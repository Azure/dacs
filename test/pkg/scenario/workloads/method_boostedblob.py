"""Boosted blob access method — reads model shards via dacs_boostedblob_adapter.

Uses the dacs_boostedblob_adapter which provides a boostedblob-compatible async
API backed by DACSCacheClient. The adapter's read.read_single() reads entire
blobs through the DACS cache layer.
"""

import asyncio
import os
import sys

sys.path.insert(0, "/app")
sys.path.insert(0, "/app/py_dacs_client")

from auth import ensure_auth


def discover_shards(config):
    """List .safetensors files under the blob prefix using blobfile (discovery only).

    Returns shard URLs with the FQDN account name for DACSCacheClient.
    """
    ensure_auth()
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


async def _read_shard_async(shard_url):
    """Read a shard using boostedblob adapter's zero-copy read_single API.

    Uses a pre-allocated buffer to avoid the ctypes.string_at >2GB limitation
    on Python 3.10 (Negative size passed to PyBytes_FromStringAndSize).

    TODO: Switch to chunked ranged reads once the adapter supports positional
    reads (seek/tell), which will avoid allocating a buffer for the entire shard.
    """
    import dacs_boostedblob_adapter as bbb
    from dacs_utils import alloc_buffer, release_buffer

    file_size = await bbb.getsize(shard_url)
    buf = alloc_buffer(file_size)
    try:
        bytes_read = await bbb.read.read_single(shard_url, buffer=buf, size=file_size)
    finally:
        release_buffer(buf)
    return bytes_read


def read_shard(shard_url, config):
    """Read a shard through the boostedblob adapter."""
    ensure_auth()
    return asyncio.run(_read_shard_async(shard_url))
