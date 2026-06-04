"""Run:AI Model Streamer access method — reads model shards via runaistreamer.

Uses the Run:AI Model Streamer Python library which provides streaming reads
from Azure Blob Storage. This method does NOT use InfiniBand or GPUDirect
Storage (GDS) — it is a CPU-only streaming path.

In no_cache mode, the streamer reads directly from Azure Blob Storage.
In cache modes (cold_cache, warm_cache), the streamer auto-discovers the DACS
cache provider (libStorageDirect.so) via dladdr in Python site-packages. The provider
exports blob_read() which routes reads through DACS's SI_open/SI_pread C API —
cache hits are served from local NVMe, misses fetch from blob and populate the cache.

BlobSiWrapper's __attribute__((constructor)) auto-generates the SI config from env vars
(AZURE_CLIENT_ID, CACHE_DISCOVERY_URL, etc.) — no manual config file needed.

Environment variables (in addition to standard harness env):
    RUNAI_STREAMER_CONCURRENCY                        - Number of concurrent streams (default: 16)
    RUNAI_STREAMER_BLOCK_SIZE                         - Read block size in bytes (default: 8MB)
    RUNAI_STREAMER_EXPERIMENTAL_AZURE_CACHE_ENABLED   - "0" to disable cache auto-discovery
    CACHE_DISCOVERY_URL                               - Cache server discovery endpoint (read by BlobSiWrapper)
    CACHE_SERVER_PORT                                 - Cache server port (read by BlobSiWrapper, default: 9065)
    STORAGE_INTCPT_MOUNT_PATH                         - Cache mount path (read by BlobSiWrapper)
    AZURE_STORAGE_ACCOUNT_NAME                        - Storage account (set by runner, used by C++ Azure SDK)

Note: The native libstreamerazure.so uses Azure SDK C++ DefaultAzureCredential for auth.
Workload identity env vars (AZURE_CLIENT_ID, AZURE_TENANT_ID, AZURE_FEDERATED_TOKEN_FILE)
are injected by the AKS webhook. The URI format is az://container/blob_path.
"""

import os
import sys

sys.path.insert(0, "/app")

from auth import ensure_auth


def _patch_file_streamer():
    """Fix handle_object_store bug (returns None for Azure URLs).

    The method only returns the path for S3 URLs, returning None for Azure
    https:// URLs. stream_files() then overwrites FileChunks.path with None,
    causing 'File access error' from the native library. This patch is a
    no-op if the bug is already fixed in the installed version.
    """
    from runai_model_streamer.file_streamer.file_streamer import FileStreamer
    _original = FileStreamer.handle_object_store

    def _fixed(self, path, credentials):
        result = _original(self, path, credentials)
        return result if result is not None else path

    FileStreamer.handle_object_store = _fixed


_patch_applied = False


def discover_shards(config):
    """List .safetensors files under the blob prefix using blobfile.

    Returns az://container/blob_path URLs for the Run:AI streamer native library.
    The native lib's URI parser only accepts s3://, gs://, or az:// schemes.
    Auth is via AZURE_STORAGE_ACCOUNT_NAME + DefaultAzureCredential (C++ Azure SDK).
    """
    ensure_auth()
    import blobfile
    account = config["storage_account"]
    container = config["container"]
    prefix = config["blob_prefix"]
    base_url = f"az://{account}/{container}/{prefix}"

    files = []
    for entry in blobfile.listdir(base_url):
        if entry.endswith(".safetensors"):
            files.append(entry)

    # Native lib URI parser regex: ^(s3|gs|az)://([^/]+)/(.+)$
    return sorted(f"az://{container}/{prefix}/{f}" for f in files)


def read_shard(shard_url, config):
    """Read a shard using the Run:AI Model Streamer.

    In no_cache mode, the streamer reads directly from Azure Blob.
    Auth is via C++ Azure SDK DefaultAzureCredential (workload identity).
    In cache modes, the streamer auto-discovers libStorageDirect.so which
    exports blob_read() — the streamer calls it automatically to serve reads
    from DACS cache (NVMe on hit, blob on miss).
    """
    global _patch_applied
    if not _patch_applied:
        _patch_file_streamer()
        _patch_applied = True

    from runai_model_streamer import FileStreamer, FileChunks

    block_size = int(os.environ.get("RUNAI_STREAMER_BLOCK_SIZE", str(8 * 1024 * 1024)))

    # Get file size via blobfile (convert az:// to blobfile's az://account/... format)
    import blobfile
    # az://container/prefix/file -> az://account/container/prefix/file
    account = config["storage_account"]
    blob_path = shard_url.removeprefix("az://")
    bf_url = f"az://{account}/{blob_path}"
    stat = blobfile.stat(bf_url)
    file_size = stat.size

    # Build chunk list: split file into block_size chunks
    chunks = []
    remaining = file_size
    while remaining > 0:
        chunk = min(block_size, remaining)
        chunks.append(chunk)
        remaining -= chunk

    total_bytes = 0
    with FileStreamer() as streamer:
        file_request = FileChunks(id=0, path=shard_url, offset=0, chunks=chunks)
        streamer.stream_files([file_request])
        for file_path, chunk_index, tensor in streamer.get_chunks():
            total_bytes += tensor.numel()

    return total_bytes
