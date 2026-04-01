"""Blobfile access method — reads model shards using the blobfile library.

In no_cache mode, uses plain blobfile (direct blob reads).
In cache modes, enables dacs_blobfile_patcher which monkey-patches
blobfile to route az:// reads through DACSAdapter.

Auth workaround: The native lib (libStorageDirect.so) used by DACSAdapter
tries IMDS auth which fails on workload identity clusters. We inject an Azure AD
access token via SI_azBlobAccessToken before enabling the patcher.

Endpoint workaround: The native lib needs FQDN for the storage account.
We set STORAGE_INTCPT_TARGET_STORAGE_ACCOUNT to <account>.blob.core.windows.net.
"""

import os
import sys

sys.path.insert(0, "/app")

import blobfile

from auth import ensure_auth

_patcher_enabled = False


def _maybe_enable_patcher(config):
    """Enable the DACS blobfile patcher for cache modes."""
    global _patcher_enabled
    if _patcher_enabled:
        return
    cache_mode = os.environ.get("CACHE_MODE", "no_cache")
    if cache_mode == "no_cache":
        return
    try:
        # Fix ctypes.string_at to support sizes > 2 GB. Python 3.10's _string_at
        # ignores argtypes patches, so we monkey-patch string_at itself to use
        # a c_char array for sizes that overflow c_int.
        import ctypes
        _original_string_at = ctypes.string_at
        def _large_string_at(ptr, size=-1):
            if size > 0x7FFFFFFF:
                if isinstance(ptr, int):
                    addr = ptr
                elif hasattr(ptr, 'value') and isinstance(ptr.value, int):
                    addr = ptr.value
                else:
                    addr = ctypes.cast(ptr, ctypes.c_void_p).value
                return bytes((ctypes.c_char * size).from_address(addr))
            return _original_string_at(ptr, size)
        ctypes.string_at = _large_string_at

        # Auth workaround: inject access token before native lib init
        ensure_auth()

        # Endpoint workaround: set FQDN so native lib constructs correct URL
        account = config["storage_account"]
        fqdn = account + ".blob.core.windows.net"
        os.environ["STORAGE_INTCPT_TARGET_STORAGE_ACCOUNT"] = fqdn

        from py_dacs_client.dacs_blobfile_patcher import enable_dacs_backend
        enable_dacs_backend()
        _patcher_enabled = True
        print(f"  Blobfile patcher enabled for cache_mode={cache_mode}", flush=True)
    except Exception as e:
        print(f"  Warning: failed to enable blobfile patcher: {e}", flush=True)


def discover_shards(config):
    """List .safetensors files under the blob prefix.

    Discovery always uses plain blobfile (before patcher is enabled).
    When patcher is active, shard URLs use FQDN account name so the native
    lib constructs the correct endpoint (https://<account>.blob.core.windows.net).
    """
    account = config["storage_account"]
    base_url = f"az://{account}/{config['container']}/{config['blob_prefix']}"
    files = []
    for entry in blobfile.listdir(base_url):
        if entry.endswith(".safetensors"):
            files.append(entry)

    # Enable patcher after discovery so reads go through DACSAdapter
    _maybe_enable_patcher(config)

    # Use FQDN URLs when patcher is enabled (native lib needs FQDN for endpoint)
    if _patcher_enabled:
        fqdn = account + ".blob.core.windows.net"
        fqdn_base = f"az://{fqdn}/{config['container']}/{config['blob_prefix']}"
        return sorted(f"{fqdn_base}/{f}" for f in files)

    return sorted(f"{base_url}/{f}" for f in files)


def read_shard(shard_url, config):
    """Read a shard using blobfile (plain or patched depending on cache mode).

    Plain blobfile: standard chunked read loop.
    Patched blobfile: single f.read() to read entire file via DACSAdapter.
    """
    if not _patcher_enabled:
        # Plain blobfile — chunked read loop
        total_bytes = 0
        with blobfile.BlobFile(shard_url, "rb") as f:
            while True:
                data = f.read(config["buffer_size"])
                if not data:
                    break
                total_bytes += len(data)
        return total_bytes

    # Patched blobfile — DACSAdapter.read(size) always reads from offset 0
    # with no internal position tracking (seek/tell raise OperationNotSupportedError),
    # so chunked reads return the same data repeatedly. This is a bug in
    # DACSAdapter; a blobfile-compatible wrapper should track position.
    # Work around it by reading the entire file in a single call.
    with blobfile.BlobFile(shard_url, "rb") as f:
        data = f.read()
        return len(data)
