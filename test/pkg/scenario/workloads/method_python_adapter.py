"""Python dacs client access method — reads model shards via DACSCacheClient.

Uses DACSCacheClient from py_dacs_client which provides direct async access
to the native cache library, with get_file_size() and read_blob() APIs.

Auth workaround: Injects an Azure AD access token as SI_azBlobAccessToken before
init, bypassing IMDS auth that fails on workload identity clusters (HTTP 400).
"""

import asyncio
import os
import sys

sys.path.insert(0, "/app")

_auth_done = False


def _ensure_auth():
    """Inject Azure AD access token for the native library."""
    global _auth_done
    if _auth_done:
        return
    _auth_done = True

    if not os.environ.get("SI_azBlobAccessToken"):
        try:
            from azure.identity import DefaultAzureCredential
            cred = DefaultAzureCredential()
            token = cred.get_token("https://storage.azure.com/.default")
            os.environ["SI_azBlobAccessToken"] = token.token
            print(f"  Injected Azure AD access token for native lib", flush=True)
        except Exception as e:
            print(f"  Warning: could not fetch access token: {e}", flush=True)


def discover_shards(config):
    """List .safetensors files under the blob prefix using blobfile (discovery only).

    Returns shard URLs with the FQDN account name for DACSCacheClient.
    """
    _ensure_auth()
    import blobfile
    account = config["storage_account"]
    fqdn = account + ".blob.core.windows.net"
    base_url = f"az://{account}/{config['container']}/{config['blob_prefix']}"
    fqdn_base = f"az://{fqdn}/{config['container']}/{config['blob_prefix']}"
    files = []
    for entry in blobfile.listdir(base_url):
        if entry.endswith(".safetensors"):
            files.append(f"{fqdn_base}/{entry}")
    return sorted(files)


async def _read_shard_async(shard_url, chunk_size):
    """Read a shard using DACSCacheClient's async read_blob API."""
    from py_dacs_client.dacs_client import DACSCacheClient
    from py_dacs_client.dacs_utils import alloc_buffer, release_buffer

    client = DACSCacheClient()
    try:
        file_size = await client.get_file_size(shard_url)
        buf = alloc_buffer(chunk_size)
        total_bytes = 0
        try:
            offset = 0
            while offset < file_size:
                read_size = min(chunk_size, file_size - offset)
                bytes_read = await client.read_blob(shard_url, buf, read_size, offset)
                if bytes_read <= 0:
                    break
                total_bytes += bytes_read
                offset += bytes_read
        finally:
            release_buffer(buf)
    finally:
        client.shutdown()
    return total_bytes


def read_shard(shard_url, config):
    """Read a shard through the DACSCacheClient."""
    _ensure_auth()
    return asyncio.run(_read_shard_async(shard_url, config["buffer_size"]))
