#!/usr/bin/env python3
# Copyright (c) Microsoft Corporation.
# Licensed under the Apache License, Version 2.0.

"""
Model Loading Scenario Harness

Common entry point that handles shard discovery, timing, and JSON result output.
The actual read logic is provided by a method module injected at /tmp/method.py.

Each method module must define:
    discover_shards(config: dict) -> list[str]
        Return list of shard identifiers (URLs, paths, or filenames).
    read_shard(shard_id: str, config: dict) -> int
        Read one shard, return total bytes read.

Environment variables:
    ACCESS_METHOD              - Access method name (for result metadata)
    AZURE_STORAGE_ACCOUNT_NAME - Azure storage account name
    STORAGE_CONTAINER          - Azure storage container name
    BLOB_PREFIX                - Blob prefix path
    HF_REPO                    - Hugging Face repo ID
    SCALE_NAME                 - Scale profile name
    CACHE_DISCOVERY_URL        - DACS cache discovery URL (empty = no cache)
    CACHE_WARM                 - "true" if warm cache pass
    READ_BUFFER_SIZE           - Read buffer size in bytes (default: 64MB)
"""

import importlib.util
import json
import os
import resource
import sys
import time
import traceback
import urllib.request

# Add /app to path for py_dacs_client
sys.path.insert(0, "/app")


def get_env(name, default=""):
    return os.environ.get(name, default)


def load_method_module():
    """Load the method module from /tmp/method.py."""
    spec = importlib.util.spec_from_file_location("method", "/tmp/method.py")
    if spec is None or spec.loader is None:
        raise ImportError("Could not load /tmp/method.py — file may be missing or unreadable")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def build_config():
    """Build config dict from environment variables."""
    return {
        "access_method": get_env("ACCESS_METHOD", "blobfile"),
        "storage_account": get_env("AZURE_STORAGE_ACCOUNT_NAME"),
        "container": get_env("STORAGE_CONTAINER", "test-data"),
        "blob_prefix": get_env("BLOB_PREFIX"),
        "hf_repo": get_env("HF_REPO"),
        "cache_discovery_url": get_env("CACHE_DISCOVERY_URL"),
        "buffer_size": int(get_env("READ_BUFFER_SIZE", str(8 * 1024 * 1024 * 1024))),
        "cache_chunk_size": int(get_env("CACHE_CHUNK_SIZE", "0")),
    }


def _wait_for_async_uploads(discovery_url, expected_chunks, timeout=120):
    """Poll cache server Prometheus metrics until async uploads complete.

    After reading with async upload enabled, chunks are uploaded in the background.
    This waits until the cache servers report enough successful uploads before the
    pod exits, preventing upload thread termination.
    """
    if not discovery_url or expected_chunks <= 0:
        return

    # Discovery URL format: svc-name.namespace.svc.cluster.local
    # Individual pods: cacheserver-{i}.cacheserver.namespace.svc.cluster.local:9096
    parts = discovery_url.split(".")
    if len(parts) < 2:
        return
    namespace = parts[1]

    def _scrape_upload_count():
        total = 0
        i = 0
        consecutive_failures = 0
        while consecutive_failures < 3:
            url = f"http://cacheserver-{i}.cacheserver.{namespace}.svc.cluster.local:9096/metrics"
            try:
                resp = urllib.request.urlopen(url, timeout=2)
                consecutive_failures = 0
                for line in resp.read().decode().split("\n"):
                    # Count both Success and InvalidTransition (already cached).
                    # InvalidTransition means the chunk was already in cache from
                    # a prior upload, so it's still a completed upload attempt.
                    if ("cache_server_request_counter" in line
                            and 'request_type="Upload"' in line
                            and not line.startswith("#")
                            and ('status="Success"' in line
                                 or 'status="InvalidTransition"' in line)):
                        total += int(float(line.split()[-1]))
            except Exception:
                consecutive_failures += 1
            i += 1
        return total

    baseline = _scrape_upload_count()
    deadline = time.time() + timeout
    while time.time() < deadline:
        uploads = _scrape_upload_count() - baseline
        print(f"  Async uploads: {uploads}/{expected_chunks}", flush=True)
        if uploads >= expected_chunks:
            print(f"  All async uploads complete", flush=True)
            return
        time.sleep(2)
    uploads = _scrape_upload_count() - baseline
    print(f"  Upload wait timed out: {uploads}/{expected_chunks}", flush=True)


def run_scenario():
    scenario_start = time.monotonic()
    config = build_config()

    print("Model Loading Scenario", flush=True)
    print(f"  Access Method: {config['access_method']}", flush=True)
    print(f"  Storage Account: {config['storage_account']}", flush=True)
    print(f"  Container: {config['container']}", flush=True)
    print(f"  Blob Prefix: {config['blob_prefix']}", flush=True)
    print(f"  HF Repo: {config['hf_repo']}", flush=True)
    print(f"  Cache Discovery: {config['cache_discovery_url'] or '(disabled)'}", flush=True)
    print(f"  Buffer Size: {config['buffer_size'] / (1024*1024):.0f} MB", flush=True)

    # Load the method module
    method = load_method_module()
    print(f"  Method module: {method.__name__}", flush=True)
    print(f"  [T+{time.monotonic()-scenario_start:.1f}s] Method loaded", flush=True)

    # Discover shards
    shards = method.discover_shards(config)
    if not shards:
        print("ERROR: No .safetensors shards found!", flush=True)
        sys.exit(1)
    print(f"  Discovered {len(shards)} shards", flush=True)
    for s in shards:
        name = s.split("/")[-1] if "/" in s else s
        print(f"    - {name}", flush=True)
    print(f"  [T+{time.monotonic()-scenario_start:.1f}s] Discovery complete", flush=True)

    # Run the loading benchmark
    shard_results = []
    total_bytes = 0
    first_byte_time = None
    first_shard_time = None
    total_start = time.monotonic()

    for i, shard_id in enumerate(shards):
        name = shard_id.split("/")[-1] if "/" in shard_id else shard_id
        print(f"  Loading shard {i+1}/{len(shards)}: {name}...", flush=True, end="")
        shard_start = time.monotonic()

        try:
            bytes_read = method.read_shard(shard_id, config)
        except Exception as e:
            print(f"\nERROR loading shard {name}: {e}", flush=True)
            traceback.print_exc()
            sys.exit(1)

        shard_end = time.monotonic()
        shard_duration = shard_end - shard_start

        if first_byte_time is None and bytes_read > 0:
            estimated_first_buf = min(
                shard_duration,
                shard_duration * config["buffer_size"] / max(bytes_read, 1),
            )
            first_byte_time = shard_start - total_start + estimated_first_buf
        if first_shard_time is None:
            first_shard_time = shard_end - total_start

        total_bytes += bytes_read
        shard_results.append({
            "name": name,
            "bytes": bytes_read,
            "duration_sec": round(shard_duration, 3),
            "throughput_mbps": round(
                bytes_read * 8 / 1_000_000 / max(shard_duration, 0.001), 1
            ),
        })
        print(
            f" {bytes_read/(1024**3):.2f} GB in {shard_duration:.1f}s "
            f"({bytes_read*8/1_000_000/max(shard_duration, 0.001):.0f} Mbps)",
            flush=True,
        )

    total_end = time.monotonic()
    total_duration = total_end - total_start
    peak_memory_mb = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / 1024
    print(f"  [T+{time.monotonic()-scenario_start:.1f}s] Read complete ({total_duration:.1f}s)", flush=True)

    cache_discovery = config["cache_discovery_url"]
    cache_warm = os.environ.get("CACHE_WARM") == "true"
    if cache_discovery and cache_warm:
        cache_mode = "warm_cache"
    elif cache_discovery:
        cache_mode = "cold_cache"
    else:
        cache_mode = "no_cache"

    # On cold_cache runs, wait for async cache uploads to complete before the pod
    # exits. Without this, background upload threads are killed with the process.
    if cache_mode == "cold_cache" and config["cache_chunk_size"] > 0:
        chunk_count = sum(
            (s["bytes"] + config["cache_chunk_size"] - 1) // config["cache_chunk_size"]
            for s in shard_results
        )
        wait_start = time.monotonic()
        _wait_for_async_uploads(cache_discovery, chunk_count)
        print(f"  [T+{time.monotonic()-scenario_start:.1f}s] Upload wait done ({time.monotonic()-wait_start:.1f}s)", flush=True)

    result = {
        "Scenario": "model_loading",
        "AccessMethod": config["access_method"],
        "CacheMode": cache_mode,
        "Scale": get_env("SCALE_NAME", ""),
        "HFRepo": config["hf_repo"],
        "ShardCount": len(shard_results),
        "TotalSizeGB": round(total_bytes / (1024**3), 3),
        "TotalLoadTimeSec": round(total_duration, 3),
        "TimeToFirstByteSec": round(first_byte_time, 3) if first_byte_time else 0,
        "TimeToFirstShardSec": round(first_shard_time, 3) if first_shard_time else 0,
        "ThroughputMbps": round(
            total_bytes * 8 / 1_000_000 / max(total_duration, 0.001), 1
        ),
        "ShardLoadTimes": [s["duration_sec"] for s in shard_results],
        "PeakMemoryMB": round(peak_memory_mb, 1),
        "CacheHitRate": 0,
    }

    print(f"\n{'='*60}", flush=True)
    print("SCENARIO RESULT SUMMARY", flush=True)
    print(f"{'='*60}", flush=True)
    print(f"  Total: {result['TotalSizeGB']:.2f} GB in {result['TotalLoadTimeSec']:.1f}s", flush=True)
    print(f"  Throughput: {result['ThroughputMbps']:.0f} Mbps", flush=True)
    print(f"  Shards: {result['ShardCount']}", flush=True)
    print(f"  TTFB: {result['TimeToFirstByteSec']:.3f}s", flush=True)
    print(f"  Peak Memory: {result['PeakMemoryMB']:.0f} MB", flush=True)
    print(f"{'='*60}", flush=True)

    print("", flush=True)
    print("=== BENCHMARK_RESULTS_JSON ===", flush=True)
    print(json.dumps(result, indent=2), flush=True)
    print("=== END_BENCHMARK_RESULTS_JSON ===", flush=True)


if __name__ == "__main__":
    run_scenario()
