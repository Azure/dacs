# Copyright (c) Microsoft Corporation.
# Licensed under the Apache License, Version 2.0.

"""Cold read worker — reads a blob through the DACS cache and reports results.

Enables the DACSCacheClient blobfile patcher so reads are routed through
the cache.  After reading, waits for any background cache-upload threads to
finish (so other waiting clients can get the data from cache before this pod
exits).

Environment variables:
    AZURE_STORAGE_ACCOUNT_NAME - Azure storage account name
    STORAGE_CONTAINER          - Azure storage container name
    POD_NAME                   - K8s pod name (set via downward API)
    CACHE_DISCOVERY_URL        - Cache server discovery URL (empty = no cache)
    CACHE_CHUNK_SIZE           - Chunk size in bytes (for upload-wait logic)
    BLOB_NAME                  - Name/path of the blob to read
    EXPECTED_SIZE              - Expected blob size in bytes
"""

import json
import os
import sys
import time
import urllib.request

sys.path.insert(0, "/app")

account = os.environ["AZURE_STORAGE_ACCOUNT_NAME"]
container = os.environ["STORAGE_CONTAINER"]
pod_name = os.environ.get("POD_NAME", "unknown")
discovery = os.environ.get("CACHE_DISCOVERY_URL", "")
blob_name = os.environ["BLOB_NAME"]
expected_size = int(os.environ["EXPECTED_SIZE"])
chunk_size = int(os.environ.get("CACHE_CHUNK_SIZE", "0"))

print(f"[{pod_name}] ColdRead read test starting", flush=True)
print(f"  Blob: {account}/{container}/{blob_name}", flush=True)
print(f"  Cache: {discovery or '(disabled)'}", flush=True)

# Inject Azure AD access token for native lib (IMDS fails on workload identity)
if not os.environ.get("SI_azBlobAccessToken"):
    from azure.identity import DefaultAzureCredential
    cred = DefaultAzureCredential()
    token = cred.get_token("https://storage.azure.com/.default")
    os.environ["SI_azBlobAccessToken"] = token.token
    print("  Injected Azure AD token", flush=True)

# Enable blobfile patcher to route reads through DACSAdapter/cache
fqdn = account + ".blob.core.windows.net"
os.environ["STORAGE_INTCPT_TARGET_STORAGE_ACCOUNT"] = fqdn

# Fix ctypes.string_at for sizes > 2GB (Python 3.10 compatibility)
import ctypes
_original_string_at = ctypes.string_at
def _large_string_at(ptr, size=-1):
    if size > 0x7FFFFFFF:
        addr = ptr if isinstance(ptr, int) else ctypes.cast(ptr, ctypes.c_void_p).value
        return bytes((ctypes.c_char * size).from_address(addr))
    return _original_string_at(ptr, size)
ctypes.string_at = _large_string_at

from py_dacs_client.dacs_blobfile_patcher import enable_dacs_backend
enable_dacs_backend()
print("  DACSCacheClient patcher enabled", flush=True)

import blobfile

# Read the blob through cache (FQDN URL required for patched blobfile)
read_url = f"az://{fqdn}/{container}/{blob_name}"
print(f"  Reading: {read_url}", flush=True)
start = time.monotonic()
size = 0
read_buf_size = 64 * 1024 * 1024  # 64 MiB chunks to avoid OOM on large files
with blobfile.BlobFile(read_url, "rb") as f:
    while True:
        chunk = f.read(read_buf_size)
        if not chunk:
            break
        size += len(chunk)
elapsed = time.monotonic() - start

if size != expected_size:
    print(f"ERROR: expected {expected_size} bytes, got {size}", flush=True)
    sys.exit(1)

throughput = size * 8 / 1_000_000 / max(elapsed, 0.001)
print(f"  Read {size} bytes in {elapsed:.1f}s ({throughput:.0f} Mbps)", flush=True)

# Wait for async cache uploads to complete (the DACSCacheClient uploads
# chunks in background threads; if the pod exits too early the upload threads
# are killed and other waiting clients never get the data from cache).
if discovery and chunk_size > 0:
    expected_chunks = (size + chunk_size - 1) // chunk_size
    parts = discovery.split(".")
    ns = parts[1] if len(parts) >= 2 else ""
    if ns:
        print(f"  Waiting for {expected_chunks} async uploads...", flush=True)
        deadline = time.time() + 120
        while time.time() < deadline:
            total = 0
            i, fails = 0, 0
            while i < 20 and fails < 3:
                url = f"http://cacheserver-{i}.cacheserver.{ns}.svc.cluster.local:9096/metrics"
                try:
                    resp = urllib.request.urlopen(url, timeout=2)
                    fails = 0
                    for line in resp.read().decode().split("\n"):
                        if ("cache_server_request_counter" in line
                                and 'request_type="Upload"' in line
                                and not line.startswith("#")
                                and ('status="Success"' in line
                                     or 'status="InvalidTransition"' in line)):
                            total += int(float(line.split()[-1]))
                except Exception:
                    fails += 1
                i += 1
            if total >= expected_chunks:
                print(f"  Async uploads done ({total})", flush=True)
                break
            time.sleep(2)

result = {
    "pod": pod_name,
    "file": blob_name,
    "size": size,
    "elapsed_sec": round(elapsed, 3),
    "throughput_mbps": round(throughput, 1),
}
print("=== COLD_READ_RESULT_JSON ===", flush=True)
print(json.dumps(result), flush=True)
print("=== END_COLD_READ_RESULT_JSON ===", flush=True)
