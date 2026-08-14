# Copyright (c) Microsoft Corporation.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Blobfuse2 access method - mounts an Azure Blob container via blobfuse2 and
reads model shards as regular files.

Configured pipeline with dist_cache disabled:
    libfuse -> block_cache -> attr_cache -> azstorage

Configured pipeline with dist_cache enabled:
    libfuse -> dist_cache -> attr_cache -> azstorage

blobfuse2 automatically inserts and configures block_cache as dist_cache's L1.
The YAML must not include a block_cache component or section in this mode.

dist_cache is included when $BLOBFUSE_USE_DIST_CACHE is "true" and uses
$BLOBFUSE_DIST_CACHE_DISCOVERY_ENDPOINT (the DACS discovery endpoint) to
locate the cache servers as an L2.

Always sets libfuse.direct-io=true so reads bypass the Linux kernel page cache
on the FUSE mount - required for warm_cache runs to actually exercise
dist_cache (DACS L2) rather than reading from stale page-cached data.

Auth: uses blobfuse2's `azstorage.mode: spn` with `oauth-token-path` pointing at
the federated token file mounted by the AKS workload-identity webhook. This is
the path that ends up using `azidentity.NewWorkloadIdentityCredential` inside
blobfuse2. The webhook injects `AZURE_CLIENT_ID`, `AZURE_TENANT_ID`, and
`AZURE_FEDERATED_TOKEN_FILE`, and this module copies them into the rendered
config. Note: blobfuse2's `mode: workloadidentity` is a different (node-
assigned MI) flow and does NOT work with AKS workload identity federation.
"""

import atexit
import os
import subprocess
import sys
import time

sys.path.insert(0, "/app")

from auth import ensure_auth  # noqa: F401 - imported for parity; blobfuse does its own auth

_mount_path = None


def _drop_page_cache():
    """Best-effort drop of the Linux page cache so warm_cache runs don't see
    pages left behind on the node by a previous pod. Requires CAP_SYS_ADMIN,
    which the workload container has via WithPrivilegedFuse. Swallow errors
    so a hardened cluster doesn't fail the run."""
    try:
        with open("/proc/sys/vm/drop_caches", "w") as f:
            f.write("3\n")
        print("  Dropped node page cache (/proc/sys/vm/drop_caches=3)", flush=True)
    except Exception as e:
        print(f"  Warning: could not drop page cache: {e}", flush=True)


def _render_config(account, container, use_dist_cache, dist_cache_discovery,
                   client_id, tenant_id, federated_token_file):
    """Render the blobfuse2 YAML config to /tmp/blobfuse.yaml."""
    lines = [
        "allow-other: false",
        "read-only: true",
        "logging:",
        "  type: base",
        "  level: log_warning",
        "  file-path: /tmp/blobfuse2.log",
        "components:",
        "  - libfuse",
    ]
    if use_dist_cache:
        lines.append("  - dist_cache")
    else:
        lines.append("  - block_cache")
    lines.extend([
        "  - attr_cache",
        "  - azstorage",
        "libfuse:",
        "  attribute-expiration-sec: 120",
        "  entry-expiration-sec: 120",
        "  negative-entry-expiration-sec: 240",
        "  direct-io: true",
    ])
    if use_dist_cache:
        lines.extend([
            "dist_cache:",
            f"  discovery-endpoint: {dist_cache_discovery}",
            # These L1 knobs are fanned out to the auto-inserted block_cache.
            "  block-size-mb: 32",
            "  prefetch: 32",
            "  parallelism: 128",
        ])
    else:
        lines.extend([
            "block_cache:",
            "  block-size-mb: 32",
            "  prefetch: 32",
            "  parallelism: 128",
            "  prefetch-on-open: true",
        ])
    lines.extend([
        "attr_cache:",
        "  timeout-sec: 7200",
        "azstorage:",
        "  type: block",
        f"  account-name: {account}",
        f"  container: {container}",
        # Use spn mode with oauth-token-path pointing at the AKS
        # workload-identity webhook's projected federated token file.
        # blobfuse2's `workloadidentity` mode is actually a VMSS-assigned MI
        # flow and does NOT work with AKS workload identity federated tokens.
        "  mode: spn",
        f"  clientid: {client_id}",
        f"  tenantid: {tenant_id}",
        f"  oauth-token-path: {federated_token_file}",
    ])
    cfg_path = "/tmp/blobfuse.yaml"
    with open(cfg_path, "w") as f:
        f.write("\n".join(lines) + "\n")
    return cfg_path


def _unmount(mount_path):
    """Best-effort unmount; safe to call multiple times."""
    if not mount_path:
        return
    try:
        subprocess.run(
            ["blobfuse2", "unmount", mount_path],
            check=False, capture_output=True, timeout=30,
        )
    except Exception as e:
        print(f"  Warning: blobfuse2 unmount failed: {e}", flush=True)


def _mount(cfg_path, mount_path):
    os.makedirs(mount_path, exist_ok=True)

    # In case a previous pod left a half-mount on the same path, force unmount.
    _unmount(mount_path)

    cmd = ["blobfuse2", "mount", mount_path, f"--config-file={cfg_path}"]
    print(f"  Running: {' '.join(cmd)}", flush=True)
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    if result.returncode != 0:
        raise RuntimeError(
            f"blobfuse2 mount failed (rc={result.returncode}):\n"
            f"STDOUT:\n{result.stdout}\nSTDERR:\n{result.stderr}"
        )

    # Wait until the mount appears in /proc/mounts.
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        if os.path.ismount(mount_path):
            print(f"  Blobfuse mount ready at {mount_path}", flush=True)
            return
        time.sleep(0.5)
    raise RuntimeError(f"blobfuse2 mount did not become ready at {mount_path}")


def _setup(config):
    """One-time setup: render config, drop page cache, mount."""
    global _mount_path
    if _mount_path is not None:
        return _mount_path

    account = config["storage_account"]
    container = config["container"]
    mount_path = os.environ.get("BLOBFUSE_MOUNT_PATH", "/mnt/blobfuse")
    use_dist_cache = os.environ.get("BLOBFUSE_USE_DIST_CACHE", "false").lower() == "true"
    dist_cache_discovery = os.environ.get("BLOBFUSE_DIST_CACHE_DISCOVERY_ENDPOINT", "")
    client_id = os.environ.get("AZURE_CLIENT_ID", "")
    # AZURE_TENANT_ID and AZURE_FEDERATED_TOKEN_FILE are injected by the AKS
    # workload-identity webhook.
    tenant_id = os.environ.get("AZURE_TENANT_ID", "")
    federated_token_file = os.environ.get(
        "AZURE_FEDERATED_TOKEN_FILE",
        "/var/run/secrets/azure/tokens/azure-identity-token",
    )
    if not client_id or not tenant_id or not os.path.exists(federated_token_file):
        raise RuntimeError(
            f"Workload identity env not fully injected: "
            f"AZURE_CLIENT_ID={'set' if client_id else 'EMPTY'}, "
            f"AZURE_TENANT_ID={'set' if tenant_id else 'EMPTY'}, "
            f"AZURE_FEDERATED_TOKEN_FILE={federated_token_file} "
            f"(exists={os.path.exists(federated_token_file)})"
        )

    if use_dist_cache and not dist_cache_discovery:
        raise RuntimeError(
            "BLOBFUSE_USE_DIST_CACHE=true but "
            "BLOBFUSE_DIST_CACHE_DISCOVERY_ENDPOINT is empty"
        )
    if use_dist_cache and ":" not in dist_cache_discovery.split("/")[-1]:
        raise RuntimeError(
            "BLOBFUSE_DIST_CACHE_DISCOVERY_ENDPOINT must include a port"
        )

    print(f"  Blobfuse mount: {mount_path}", flush=True)
    print(f"  dist_cache: {'on (' + dist_cache_discovery + ')' if use_dist_cache else 'off'}",
          flush=True)

    _drop_page_cache()
    cfg_path = _render_config(
        account=account,
        container=container,
        use_dist_cache=use_dist_cache,
        dist_cache_discovery=dist_cache_discovery,
        client_id=client_id,
        tenant_id=tenant_id,
        federated_token_file=federated_token_file,
    )
    _mount(cfg_path, mount_path)

    atexit.register(_unmount, mount_path)
    _mount_path = mount_path
    return mount_path


def discover_shards(config):
    """List .safetensors files under the blob prefix on the mounted fs."""
    mount_path = _setup(config)
    prefix_dir = os.path.join(mount_path, config["blob_prefix"])
    if not os.path.isdir(prefix_dir):
        raise RuntimeError(
            f"Blob prefix not visible on mount: {prefix_dir} "
            f"(blob_prefix={config['blob_prefix']})"
        )
    shards = sorted(
        os.path.join(prefix_dir, name)
        for name in os.listdir(prefix_dir)
        if name.endswith(".safetensors")
    )
    return shards


def read_shard(shard_path, config):
    """Read a shard from the FUSE mount with a plain chunked read loop."""
    total_bytes = 0
    buffer_size = config["buffer_size"]
    with open(shard_path, "rb") as f:
        while True:
            data = f.read(buffer_size)
            if not data:
                break
            total_bytes += len(data)
    return total_bytes
