"""Python dacs client access method — reads model shards via DACSCacheClient.

Uses DACSCacheClient from py_dacs_client which provides direct async access
to the native cache library, with get_file_size() and read_blob() APIs.
"""

import asyncio
import os
import sys

sys.path.insert(0, "/app")

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
    ensure_auth()
    return asyncio.run(_read_shard_async(shard_url, config["buffer_size"]))
