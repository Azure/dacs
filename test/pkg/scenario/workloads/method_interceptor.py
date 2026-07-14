# Copyright (c) Microsoft Corporation.
# Licensed under the Apache License, Version 2.0.

"""Interceptor access method — reads shards via LD_PRELOAD storage intercept.

Requires LD_PRELOAD=/app/py_dacs_client/libStorageIntercept.so set in the
container environment. The interceptor transparently redirects POSIX file I/O
through the DACS cache layer.

Config setup: The interceptor needs a config file at STORAGE_INTERCEPT_CONFIG_FILEPATH
with azBlobStorageAccountName, azBlobStorageEndpoint (FQDN), azBlobContainerName,
and authentication (azBlobAccessToken). The harness calls setup_config() before
read_shard() to write this file.
"""

import os
import sys

sys.path.insert(0, "/app")

_config_done = False


def setup_config(config):
    """Write interceptor config file with auth token and endpoint before any reads."""
    global _config_done
    if _config_done:
        return
    _config_done = True

    account = config["storage_account"]
    container = config["container"]
    config_path = os.environ.get("STORAGE_INTERCEPT_CONFIG_FILEPATH", "/tmp/storageIntercept.config")

    # Get Azure AD access token
    from azure.identity import DefaultAzureCredential
    cred = DefaultAzureCredential()
    token = cred.get_token("https://storage.azure.com/.default")

    with open(config_path, "w") as f:
        f.write(f"storagePath /mnt/dacs_cache\n")
        f.write(f"type blob\n")
        f.write(f"azBlobStorageAccountName {account}\n")
        f.write(f"azBlobStorageEndpoint https://{account}.blob.core.windows.net\n")
        f.write(f"azBlobContainerName {container}\n")
        f.write(f"azBlobAccessToken {token.token}\n")
        f.write(f"maxConnsPerBlobClient 84\n")
        f.write(f"attributesCacheEnable true\n")
        f.write(f"enableWrite false\n")
        # Cache config from env vars (set by runner.go for cache modes).
        # The interceptor uses cacheEnable + cacheServerDiscoveryEnabled/Endpoint.
        for key in ("cacheEnable", "cacheEnableRemote", "cacheServerPort",
                     "cacheServerDiscoveryEnabled",
                     "cacheServerDiscoveryEndpoint",
                     "cacheServerDiscoveryIntervalInSeconds",
                     "cacheKeyExpirySeconds", "cacheIBEnabled"):
            val = os.environ.get(f"SI_{key}", "")
            if val:
                f.write(f"{key} {val}\n")

    os.environ["STORAGE_INTERCEPT_CONFIG_FILEPATH"] = config_path
    print(f"  Wrote interceptor config to {config_path}", flush=True)


def discover_shards(config):
    """List .safetensors files under the blob prefix using blobfile (discovery only)."""
    import blobfile
    base_url = f"az://{config['storage_account']}/{config['container']}/{config['blob_prefix']}"
    files = []
    for entry in blobfile.listdir(base_url):
        if entry.endswith(".safetensors"):
            files.append(entry)  # Just the filename
    return sorted(files)


def read_shard(shard_name, config):
    """Read a shard via standard open() with the storage interceptor active.

    The interceptor maps /mnt/dacs_cache/<blob_prefix>/<file> paths
    to Azure Blob Storage and routes through the DACS cache.
    """
    setup_config(config)
    path = f"/mnt/dacs_cache/{config['blob_prefix']}/{shard_name}"
    total_bytes = 0
    buf = bytearray(config["buffer_size"])
    with open(path, "rb") as f:
        while True:
            n = f.readinto(buf)
            if not n:
                break
            total_bytes += n
    return total_bytes
