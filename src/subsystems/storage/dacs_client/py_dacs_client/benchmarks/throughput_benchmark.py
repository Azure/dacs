#!/usr/bin/env python3
"""
Throughput Benchmark for DACS Client

This benchmark measures maximum achievable throughput for reading and writing
blobs to Azure Storage via the DACS caching layer. It supports multi-process
mode to saturate NIC bandwidth, or single-process mode for profiling.

Key features:
- Separate WRITE and READ phases for accurate per-operation measurement
- Multi-process mode (ProcessPoolExecutor) for maximum throughput
- Single-process mode for profiling (all I/O in main process)
- Zero-copy buffer I/O for optimal performance
- Automatic NIC speed detection and utilization percentage
- Configurable blob sizes, counts, and write modes

Usage:
    # Multi-process mode (default) - saturates NIC bandwidth
    python -m py_dacs_client.benchmarks.throughput_benchmark \\
        --storage-account <account> --num-processes 16 --num-blobs 4 --blob-size-gb 1

    # Single-process mode for profiling with gperftools
    LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libprofiler.so CPUPROFILE=/tmp/dacs.prof \\
        python -m py_dacs_client.benchmarks.throughput_benchmark \\
        --single-process --num-blobs 2 --blob-size-gb 1
"""
# --------------------------------------------------------------------------------------------------------------------
# Throughput benchmark for DACS Adapter API
# Supports multi-process mode for NIC saturation, single-process mode for profiling
# --------------------------------------------------------------------------------------------------------------------

import argparse
import json
import logging
import os
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ProcessPoolExecutor, as_completed
from dataclasses import dataclass
from datetime import datetime, timezone
from typing import List, Optional, Tuple

logger = logging.getLogger(__name__)

GB = 1024 * 1024 * 1024
MB = 1024 * 1024


def get_nic_speed() -> tuple[str, float]:
    """Get NIC speed from ethtool. Returns (interface, speed_gbps)."""
    import shutil
    
    # Check if ethtool is available
    if not shutil.which("ethtool"):
        return "unknown", 0.0
    
    permission_denied = False
    
    # Try common interfaces
    for iface in ["eth0", "ens5", "enp0s3", "eno1"]:
        try:
            result = subprocess.run(
                ["ethtool", iface],
                capture_output=True, text=True, timeout=5
            )
            
            if result.returncode == 0:
                for line in result.stdout.split("\n"):
                    if "Speed:" in line:
                        # Parse "Speed: 50000Mb/s" -> 50.0 Gbps
                        speed_str = line.split(":")[1].strip()
                        if "Mb/s" in speed_str:
                            speed_mbps = float(speed_str.replace("Mb/s", ""))
                            return iface, speed_mbps / 1000
                        elif "Gb/s" in speed_str:
                            return iface, float(speed_str.replace("Gb/s", ""))
            elif "Permission denied" in result.stderr or "Operation not permitted" in result.stderr:
                permission_denied = True
            # returncode != 0 with "No such device" is expected for missing interfaces
            
        except subprocess.TimeoutExpired:
            continue
        except Exception:
            continue
    
    # Log permission issues once after trying all interfaces
    if permission_denied:
        import sys
        print("Warning: ethtool requires elevated permissions to read NIC speed. "
              "Run with sudo or add CAP_NET_ADMIN capability.", file=sys.stderr)
    
    return "unknown", 0.0


@dataclass
class ProcessResult:
    process_id: int
    operation: str
    client: str
    bytes_transferred: int
    duration_seconds: float
    throughput_mbps: float
    throughput_gbits: float
    success: bool
    paths: List[str] = None
    error: Optional[str] = None


# Client classification constants
BLOBFILE_CLIENTS = {"blobfile", "dacs_blobfile", "dacs_blobfile_zero_copy"}
BOOSTEDBLOB_CLIENTS = {"boostedblob", "dacs_boostedblob", "dacs_boostedblob_zero_copy"}
DACS_BLOBFILE_CLIENTS = {"dacs_blobfile", "dacs_blobfile_zero_copy"}
DACS_BOOSTEDBLOB_CLIENTS = {"dacs_boostedblob", "dacs_boostedblob_zero_copy"}
ZERO_COPY_CLIENTS = {"dacs_blobfile_zero_copy", "dacs_boostedblob_zero_copy"}
ALL_VALID_CLIENTS = BLOBFILE_CLIENTS | BOOSTEDBLOB_CLIENTS


def is_dacs_client(client: str) -> bool:
    """Check if client uses a dacs adapter."""
    return client in DACS_BLOBFILE_CLIENTS or client in DACS_BOOSTEDBLOB_CLIENTS


def is_blobfile_family(client: str) -> bool:
    """Check if client belongs to the blobfile family."""
    return client in BLOBFILE_CLIENTS


def is_boostedblob_family(client: str) -> bool:
    """Check if client belongs to the boostedblob family."""
    return client in BOOSTEDBLOB_CLIENTS


def is_zero_copy(client: str) -> bool:
    """Check if client uses zero-copy mode."""
    return client in ZERO_COPY_CLIENTS


def normalize_path_for_client(path: str, client: str) -> str:
    """
    Normalize a blob path for the given client.
    
    - blobfile and boostedblob use short form: az://{account}/{container}/{blob}
    - dacs adapters use long form: az://{account}.blob.core.windows.net/{container}/{blob}
    
    Args:
        path: The input path (may be short or long form)
        client: The target client name
        
    Returns:
        Path in the appropriate format for the client
    """
    if is_dacs_client(client):
        # DACS adapters need long form
        if ".blob.core.windows.net" not in path:
            # Convert short form to long form
            parts = path.replace("az://", "").split("/", 2)
            if len(parts) >= 2:
                account = parts[0]
                container_name = parts[1]
                blob_path = parts[2] if len(parts) > 2 else ""
                return f"az://{account}.blob.core.windows.net/{container_name}/{blob_path}"
        return path
    else:
        # blobfile and boostedblob use short form
        if ".blob.core.windows.net" in path:
            # Convert long form to short form
            parts = path.replace("az://", "").split("/", 2)
            if len(parts) >= 2:
                account = parts[0].replace(".blob.core.windows.net", "")
                container_name = parts[1]
                blob_path = parts[2] if len(parts) > 2 else ""
                return f"az://{account}/{container_name}/{blob_path}"
        return path


def generate_blob_path(storage_account: str, container: str, blob_name: str, client: str) -> str:
    """
    Generate a blob path in the appropriate format for the given client.
    
    Args:
        storage_account: Azure storage account name
        container: Container name
        blob_name: Blob path within the container
        client: The target client name
        
    Returns:
        Path in the appropriate format for the client
    """
    if is_dacs_client(client):
        # DACS adapters use long form
        return f"az://{storage_account}.blob.core.windows.net/{container}/{blob_name}"
    else:
        # blobfile and boostedblob use short form
        return f"az://{storage_account}/{container}/{blob_name}"


def run_write_process(
    process_id: int,
    storage_account: str,
    container: str,
    blob_size_gb: float,
    num_blobs: int,
    write_mode: str,
    prefix: str,
    client: str = "dacs_blobfile_zero_copy",
    existing_paths: Optional[List[str]] = None,
    consistent_mode: bool = False,
) -> ProcessResult:
    """Run write benchmark in a subprocess.
    
    Args:
        client: One of:
            - "blobfile": baseline blobfile library
            - "dacs_blobfile": dacs blobfile adapter with memcpy
            - "dacs_blobfile_zero_copy": dacs blobfile adapter with zero-copy
            - "boostedblob": baseline boostedblob library
            - "dacs_boostedblob": dacs boostedblob adapter with memcpy
            - "dacs_boostedblob_zero_copy": dacs boostedblob adapter with zero-copy
        write_mode: DACS write mode (only for dacs clients)
        existing_paths: If provided, write to these paths (overwrite) instead of generating new ones.
    """
    import asyncio
    import ctypes
    import uuid
    
    blob_size = int(blob_size_gb * GB)
    
    # Use existing paths (overwrite) or generate new ones
    overwrite = existing_paths is not None
    if existing_paths:
        paths = existing_paths
    else:
        paths = []
        for i in range(num_blobs):
            unique_id = uuid.uuid4().hex[:8]
            blob_name = f"{prefix}/proc_{process_id:02d}/blob_{i:04d}_{unique_id}.bin"
            paths.append(generate_blob_path(storage_account, container, blob_name, client))
    
    # Prepare data - generate a random pattern for filling blobs.
    # For zero-copy clients the pattern is copied directly into a native buffer,
    # so we defer the full data_bytes expansion to avoid a redundant allocation.
    PATTERN_SIZE = min(1 * MB, blob_size)
    pattern = os.urandom(PATTERN_SIZE)
    data_bytes = None
    
    def get_data_bytes():
        nonlocal data_bytes
        if data_bytes is None:
            data_bytes = (pattern * (blob_size // PATTERN_SIZE + 1))[:blob_size]
        return data_bytes
    
    # Import and setup client based on type
    dacs_blobfile_shutdown = None
    dacs_boostedblob_shutdown = None
    buffer = None
    release_buffer = None
    write_fn = None  # Will hold the write function for this client
    
    if is_dacs_client(client) and is_blobfile_family(client):
        try:
            from py_dacs_client import WriteMode
            from py_dacs_client.dacs_blobfile_adapter import DACSBlobfileAdapter as DACSAdapter, shutdown as dacs_blobfile_shutdown
            from py_dacs_client.dacs_utils import alloc_buffer, release_buffer
        except ImportError as e:
            return ProcessResult(
                process_id=process_id, operation="write", client=client,
                bytes_transferred=0, duration_seconds=0, throughput_mbps=0, throughput_gbits=0,
                success=False, paths=[], error=str(e),
            )
        
        mode_map = {
            "write_sync": WriteMode.WRITE_SYNC,
            "write_cache_only": WriteMode.WRITE_CACHE_ONLY,
            "write_blob_only": WriteMode.WRITE_BLOB_ONLY,
            "write_cache_blob_async": WriteMode.WRITE_CACHE_BLOB_ASYNC,
        }
        wm = mode_map.get(write_mode, WriteMode.WRITE_SYNC)
        
        if is_zero_copy(client):
            # Fill native buffer directly in chunks to avoid double memory (5GB Python + 5GB native)
            buffer = alloc_buffer(blob_size)
            buf_addr = ctypes.cast(buffer, ctypes.c_void_p).value
            for offset in range(0, blob_size, PATTERN_SIZE):
                chunk_len = min(PATTERN_SIZE, blob_size - offset)
                ctypes.memmove(buf_addr + offset, pattern, chunk_len)
            def write_fn(path):
                with DACSAdapter(path, "wb", write_mode=wm, consistent_mode=consistent_mode) as f:
                    f.write(buffer=buffer, size=blob_size)
        else:
            def write_fn(path):
                with DACSAdapter(path, "wb", write_mode=wm, consistent_mode=consistent_mode) as f:
                    f.write(get_data_bytes())
    
    elif is_dacs_client(client) and is_boostedblob_family(client):
        try:
            from py_dacs_client import dacs_boostedblob_adapter as tbb
            from py_dacs_client.dacs_utils import alloc_buffer, release_buffer
            dacs_boostedblob_shutdown = tbb.shutdown
        except ImportError as e:
            return ProcessResult(
                process_id=process_id, operation="write", client=client,
                bytes_transferred=0, duration_seconds=0, throughput_mbps=0, throughput_gbits=0,
                success=False, paths=[], error=str(e),
            )
        
        # Initialize singleton with consistent_mode before any write calls
        tbb._ClientManager.get_instance(consistent_mode=consistent_mode)
        
        if is_zero_copy(client):
            # Fill native buffer directly in chunks to avoid double memory (5GB Python + 5GB native)
            buffer = alloc_buffer(blob_size)
            buf_addr = ctypes.cast(buffer, ctypes.c_void_p).value
            for offset in range(0, blob_size, PATTERN_SIZE):
                chunk_len = min(PATTERN_SIZE, blob_size - offset)
                ctypes.memmove(buf_addr + offset, pattern, chunk_len)
            def write_fn(path):
                asyncio.run(tbb.write.write_single(path, buffer=buffer, size=blob_size, overwrite=overwrite))
        else:
            def write_fn(path):
                asyncio.run(tbb.write.write_single(path, get_data_bytes(), overwrite=overwrite))
    
    elif is_blobfile_family(client):  # baseline blobfile
        try:
            import blobfile as bf
        except ImportError as e:
            return ProcessResult(
                process_id=process_id, operation="write", client=client,
                bytes_transferred=0, duration_seconds=0, throughput_mbps=0, throughput_gbits=0,
                success=False, paths=[], error=str(e),
            )
        
        def write_fn(path):
            with bf.BlobFile(path, "wb") as f:
                f.write(get_data_bytes())
    
    elif is_boostedblob_family(client):  # baseline boostedblob
        try:
            import boostedblob as bbb
        except ImportError as e:
            return ProcessResult(
                process_id=process_id, operation="write", client=client,
                bytes_transferred=0, duration_seconds=0, throughput_mbps=0, throughput_gbits=0,
                success=False, paths=[], error=str(e),
            )
        
        def write_fn(path):
            asyncio.run(bbb.write.write_single(path, get_data_bytes()))
    
    written_paths = []
    total_bytes = 0
    
    try:
        start = time.perf_counter()
        
        for path in paths:
            write_fn(path)
            written_paths.append(path)
            total_bytes += blob_size
        
        duration = time.perf_counter() - start
        throughput_mbps = (total_bytes / MB) / duration if duration > 0 else 0
        throughput_gbits = (throughput_mbps * 8) / 1000
        
        return ProcessResult(
            process_id=process_id, operation="write", client=client,
            bytes_transferred=total_bytes, duration_seconds=duration,
            throughput_mbps=throughput_mbps, throughput_gbits=throughput_gbits,
            success=True, paths=written_paths,
        )
    except Exception as e:
        return ProcessResult(
            process_id=process_id, operation="write", client=client,
            bytes_transferred=0, duration_seconds=0, throughput_mbps=0, throughput_gbits=0,
            success=False, paths=written_paths, error=str(e),
        )
    finally:
        if buffer is not None and release_buffer is not None:
            release_buffer(buffer)
        if dacs_blobfile_shutdown is not None:
            try:
                dacs_blobfile_shutdown()
            except Exception as e:
                print(f"[Process {process_id}] Warning: dacs_blobfile_shutdown() failed: {e}", file=sys.stderr)
        if dacs_boostedblob_shutdown is not None:
            try:
                asyncio.run(dacs_boostedblob_shutdown())
            except Exception as e:
                print(f"[Process {process_id}] Warning: dacs_boostedblob_shutdown() failed: {e}", file=sys.stderr)


def run_read_process(
    process_id: int,
    paths: List[str],
    blob_size: int,
    client: str = "dacs_blobfile_zero_copy",
) -> ProcessResult:
    """Run read benchmark in a subprocess.
    
    Args:
        client: One of:
            - "blobfile": baseline blobfile library
            - "dacs_blobfile": dacs blobfile adapter with memcpy
            - "dacs_blobfile_zero_copy": dacs blobfile adapter with zero-copy
            - "boostedblob": baseline boostedblob library
            - "dacs_boostedblob": dacs boostedblob adapter with memcpy
            - "dacs_boostedblob_zero_copy": dacs boostedblob adapter with zero-copy
    """
    import asyncio
    
    # Import and setup client based on type
    dacs_blobfile_shutdown = None
    dacs_boostedblob_shutdown = None
    buffer = None
    release_buffer = None
    read_fn = None  # Will hold the read function for this client
    
    if is_dacs_client(client) and is_blobfile_family(client):
        try:
            from py_dacs_client.dacs_blobfile_adapter import DACSBlobfileAdapter as DACSAdapter, shutdown as dacs_blobfile_shutdown
            from py_dacs_client.dacs_utils import alloc_buffer, release_buffer
        except ImportError as e:
            return ProcessResult(
                process_id=process_id, operation="read", client=client,
                bytes_transferred=0, duration_seconds=0, throughput_mbps=0, throughput_gbits=0,
                success=False, error=str(e),
            )
        
        if is_zero_copy(client):
            buffer = alloc_buffer(blob_size)
            def read_fn(path):
                with DACSAdapter(path, "rb") as f:
                    f.read(size=blob_size, buffer=buffer)
        else:
            def read_fn(path):
                with DACSAdapter(path, "rb") as f:
                    f.read(size=blob_size)
    
    elif is_dacs_client(client) and is_boostedblob_family(client):
        try:
            from py_dacs_client import dacs_boostedblob_adapter as tbb
            from py_dacs_client.dacs_utils import alloc_buffer, release_buffer
            dacs_boostedblob_shutdown = tbb.shutdown
        except ImportError as e:
            return ProcessResult(
                process_id=process_id, operation="read", client=client,
                bytes_transferred=0, duration_seconds=0, throughput_mbps=0, throughput_gbits=0,
                success=False, error=str(e),
            )
        
        if is_zero_copy(client):
            buffer = alloc_buffer(blob_size)
            def read_fn(path):
                asyncio.run(tbb.read.read_single(path, buffer=buffer, size=blob_size))
        else:
            def read_fn(path):
                asyncio.run(tbb.read.read_single(path))
    
    elif is_blobfile_family(client):  # baseline blobfile
        try:
            import blobfile as bf
        except ImportError as e:
            return ProcessResult(
                process_id=process_id, operation="read", client=client,
                bytes_transferred=0, duration_seconds=0, throughput_mbps=0, throughput_gbits=0,
                success=False, error=str(e),
            )
        
        def read_fn(path):
            with bf.BlobFile(path, "rb") as f:
                f.read()
    
    elif is_boostedblob_family(client):  # baseline boostedblob
        try:
            import boostedblob as bbb
        except ImportError as e:
            return ProcessResult(
                process_id=process_id, operation="read", client=client,
                bytes_transferred=0, duration_seconds=0, throughput_mbps=0, throughput_gbits=0,
                success=False, error=str(e),
            )
        
        def read_fn(path):
            asyncio.run(bbb.read.read_single(path))
    
    total_bytes = 0
    
    try:
        start = time.perf_counter()
        
        for path in paths:
            read_path = normalize_path_for_client(path, client)
            read_fn(read_path)
            total_bytes += blob_size
        
        duration = time.perf_counter() - start
        throughput_mbps = (total_bytes / MB) / duration if duration > 0 else 0
        throughput_gbits = (throughput_mbps * 8) / 1000
        
        return ProcessResult(
            process_id=process_id, operation="read", client=client,
            bytes_transferred=total_bytes, duration_seconds=duration,
            throughput_mbps=throughput_mbps, throughput_gbits=throughput_gbits,
            success=True,
        )
    except Exception as e:
        return ProcessResult(
            process_id=process_id, operation="read", client=client,
            bytes_transferred=0, duration_seconds=0, throughput_mbps=0, throughput_gbits=0,
            success=False, error=str(e),
        )
    finally:
        if buffer is not None and release_buffer is not None:
            release_buffer(buffer)
        if dacs_blobfile_shutdown is not None:
            try:
                dacs_blobfile_shutdown()
            except Exception as e:
                print(f"[Process {process_id}] Warning: dacs_blobfile_shutdown() failed: {e}", file=sys.stderr)
        if dacs_boostedblob_shutdown is not None:
            try:
                asyncio.run(dacs_boostedblob_shutdown())
            except Exception as e:
                print(f"[Process {process_id}] Warning: dacs_boostedblob_shutdown() failed: {e}", file=sys.stderr)


def cleanup_paths(all_paths: List[str]):
    """Clean up all blobs. Uses Azure SDK with SAS token if available, otherwise blobfile (for managed identity)."""
    print("\nCleaning up blobs...")
    
    # Get SAS token from environment
    sas_token = os.environ.get("STORAGE_INTCPT_SAS_TOKEN", "")
    
    if sas_token:
        # Use Azure SDK with SAS token
        _cleanup_with_sas(all_paths, sas_token)
    else:
        # Fall back to blobfile (works with managed identity)
        _cleanup_with_blobfile(all_paths)


def _cleanup_with_sas(all_paths: List[str], sas_token: str):
    """Clean up blobs using Azure SDK with SAS token."""
    try:
        from azure.storage.blob import ContainerClient
        
        cleaned = 0
        failed: List[Tuple[str, str]] = []
        
        # Group paths by account/container for efficient batch deletion
        container_blobs: Dict[str, List[str]] = {}
        for path in all_paths:
            if ".blob.core.windows.net" in path:
                parts = path.replace("az://", "").split("/", 2)
                if len(parts) >= 3:
                    account_host = parts[0]
                    container_name = parts[1]
                    blob_path = parts[2]
                    key = f"{account_host}/{container_name}"
                    if key not in container_blobs:
                        container_blobs[key] = []
                    container_blobs[key].append(blob_path)
        
        # Delete blobs by container
        for key, blob_paths in container_blobs.items():
            account_host, container_name = key.split("/", 1)
            container_url = f"https://{account_host}/{container_name}?{sas_token}"
            
            try:
                container_client = ContainerClient.from_container_url(container_url)
                for blob_path in blob_paths:
                    try:
                        container_client.delete_blob(blob_path)
                        cleaned += 1
                    except Exception as e:
                        error_msg = str(e)
                        if "BlobNotFound" not in error_msg:
                            failed.append((blob_path, error_msg[:100]))
            except Exception as e:
                failed.append((key, f"Container error: {str(e)[:100]}"))
        
        print(f"Cleaned up {cleaned}/{len(all_paths)} blobs.")
        
        if failed:
            print(f"  ⚠ {len(failed)} blob(s) failed to delete:")
            for path, error in failed[:5]:
                short_path = path if len(path) <= 60 else f"...{path[-57:]}"
                print(f"    - {short_path}: {error}")
            if len(failed) > 5:
                print(f"    ... and {len(failed) - 5} more")
                
    except ImportError:
        print("  ⚠ azure-storage-blob not available, trying blobfile...")
        _cleanup_with_blobfile(all_paths)


def _cleanup_with_blobfile(all_paths: List[str]):
    """Clean up blobs using blobfile (works with managed identity)."""
    try:
        import blobfile as bf
        cleaned = 0
        failed: List[Tuple[str, str]] = []
        
        for path in all_paths:
            try:
                if ".blob.core.windows.net" in path:
                    parts = path.replace("az://", "").split("/", 2)
                    if len(parts) >= 2:
                        account = parts[0].replace(".blob.core.windows.net", "")
                        container_name = parts[1]
                        blob_path = parts[2] if len(parts) > 2 else ""
                        cleanup_path = f"az://{account}/{container_name}/{blob_path}"
                        try:
                            bf.remove(cleanup_path)
                            cleaned += 1
                        except FileNotFoundError:
                            pass  # Already deleted
                else:
                    failed.append((path, "Invalid path format"))
            except Exception as e:
                error_msg = str(e)
                # Ignore transient errors during cleanup
                if "PublicAccessNotPermitted" not in error_msg and "ServerBusy" not in error_msg:
                    failed.append((path, error_msg[:100]))
        
        print(f"Cleaned up {cleaned}/{len(all_paths)} blobs.")
        
        if failed:
            print(f"  ⚠ {len(failed)} blob(s) failed to delete:")
            for path, error in failed[:5]:
                short_path = path if len(path) <= 60 else f"...{path[-57:]}"
                print(f"    - {short_path}: {error}")
            if len(failed) > 5:
                print(f"    ... and {len(failed) - 5} more")
                
    except ImportError:
        print("  ⚠ blobfile not available for cleanup")


def main():
    parser = argparse.ArgumentParser(
        description="Throughput Benchmark - Measure READ and WRITE throughput separately",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    
    parser.add_argument("--storage-account", default="storageinterceptionpr1", help="Azure storage account")
    parser.add_argument("--container", default="dataset", help="Azure blob container")
    parser.add_argument("--blob-size-gb", type=float, default=1.0, help="Size of each blob in GB")
    parser.add_argument("--num-blobs", type=int, default=4, help="Number of blobs PER PROCESS")
    parser.add_argument("--num-processes", type=int, default=8, help="Number of parallel processes")
    parser.add_argument("--write-mode", default="write_sync", help="DACS write mode")
    parser.add_argument("--consistent-mode", action="store_true", help="Enable consistent mode (overwrite support)")
    parser.add_argument("--skip-write", action="store_true", help="Skip write phase")
    parser.add_argument("--skip-read", action="store_true", help="Skip read phase")
    parser.add_argument("--no-cleanup", action="store_true", help="Skip cleanup")
    parser.add_argument("--single-process", action="store_true", 
                        help="Run all I/O in main process (for profiling). Ignores --num-processes.")
    parser.add_argument("--node-id", default=None, 
                        help="Unique node identifier to avoid path clashes in multi-node runs. "
                             "If not provided, uses hostname.")
    parser.add_argument("--clients", default="dacs_blobfile_zero_copy",
                        help="Comma-separated list of clients to benchmark: "
                             "blobfile, dacs_blobfile, dacs_blobfile_zero_copy, "
                             "boostedblob, dacs_boostedblob, dacs_boostedblob_zero_copy")
    parser.add_argument("--verbose", action="store_true", help="Enable verbose logging")
    parser.add_argument("--temp-dir",
                        help="Temp directory for blobfile staging (default: system temp). "
                             "Use a path with enough space for large files.")
    parser.add_argument("--output", help="Output file path for JSON results")
    parser.add_argument("--paths-file",
                        help="Path to JSON file for saving/loading blob paths between phases. "
                             "Used with --skip-read (saves paths) or --skip-write (loads paths) "
                             "to enable two-phase benchmarks with a global barrier between writes and reads.")
    
    args = parser.parse_args()
    
    # Parse clients list
    clients = [c.strip() for c in args.clients.split(",")]
    for client in clients:
        if client not in ALL_VALID_CLIENTS:
            parser.error(f"Invalid client '{client}'. Must be one of: {', '.join(sorted(ALL_VALID_CLIENTS))}")
    
    # Set up logging
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )
    
    # Set custom temp directory for blobfile staging (avoids /tmp space issues)
    if args.temp_dir:
        os.makedirs(args.temp_dir, exist_ok=True)
        os.environ["TMPDIR"] = args.temp_dir
        tempfile.tempdir = args.temp_dir
        logger.info(f"Using temp directory: {args.temp_dir}")
    
    # In single-process mode, force num_processes=1
    if args.single_process:
        args.num_processes = 1
    
    total_data_gb = args.num_processes * args.num_blobs * args.blob_size_gb
    blob_size = int(args.blob_size_gb * GB)
    
    # When consistent mode is active, an overwrite pass is automatically included.
    consistent_mode = args.consistent_mode
    
    # Get node identifier for unique paths in multi-node runs
    import socket
    node_id = args.node_id if args.node_id else socket.gethostname()
    prefix = f"dacs_tests/benchmark/{datetime.now(timezone.utc).strftime('%Y%m%d_%H%M%S')}/{node_id}"
    
    mode_str = "SINGLE-PROCESS (PROFILING)" if args.single_process else "MULTI-PROCESS"
    operating_mode = "CONSISTENT" if consistent_mode else "EFFICIENT"
    print("\n" + "=" * 70)
    print(f"{mode_str} BENCHMARK ({operating_mode} MODE)")
    print("=" * 70)
    print(f"Node ID:         {node_id}")
    if not args.single_process:
        print(f"Processes:       {args.num_processes}")
    print(f"Blobs:           {args.num_blobs}")
    print(f"Blob Size:       {args.blob_size_gb} GB")
    print(f"Total Data:      {total_data_gb:.1f} GB")
    print(f"Storage Account: {args.storage_account}")
    print(f"Clients:         {', '.join(clients)}")
    print(f"Operating Mode:  {operating_mode}")
    if consistent_mode:
        print(f"Overwrite:       enabled (auto)")
    print("=" * 70)
    
    # Get NIC speed once (used for all clients)
    nic_iface, nic_speed_gbps = get_nic_speed()
    
    # Store results per client for final comparison
    client_results = {}
    
    for client in clients:
        print(f"\n{'#'*70}")
        print(f"# CLIENT: {client}")
        print(f"{'#'*70}")
        
        all_written_paths: List[List[str]] = [[] for _ in range(args.num_processes)]
        
        # Initialize result tracking variables
        successful_writes: List[ProcessResult] = []
        successful_reads: List[ProcessResult] = []
        successful_overwrites: List[ProcessResult] = []
        total_written = 0
        total_read = 0
        total_overwritten = 0
        write_duration = 0.0
        read_duration = 0.0
        overwrite_duration = 0.0
        write_throughput_mbps = 0.0
        write_throughput_gbits = 0.0
        read_throughput_mbps = 0.0
        read_throughput_gbits = 0.0
        overwrite_throughput_mbps = 0.0
        overwrite_throughput_gbits = 0.0
        
        # Use unique prefix per client to avoid path clashes
        client_prefix = f"{prefix}/{client}"
    
        # ==================== WRITE PHASE ====================
        if not args.skip_write:
            print(f"\n{'='*70}")
            if args.single_process:
                print(f"WRITE PHASE - Single process (profiling mode)...")
            else:
                print(f"WRITE PHASE - Launching {args.num_processes} parallel processes...")
            print(f"{'='*70}")
            
            write_start = time.perf_counter()
            write_results: List[ProcessResult] = []
            
            if args.single_process:
                # Run directly in main process for profiling
                result = run_write_process(
                    0, args.storage_account, args.container,
                    args.blob_size_gb, args.num_blobs, args.write_mode, client_prefix,
                    client, consistent_mode=consistent_mode,
                )
                write_results.append(result)
                if result.paths:
                    all_written_paths[0] = result.paths
                status = "✓" if result.success else "✗"
                print(f"  {status} {result.throughput_mbps:.0f} MB/s ({result.throughput_gbits:.2f} Gbits/s)")
                if result.error:
                    print(f"    Error: {result.error}")
            else:
                # Multi-process mode for NIC saturation
                with ProcessPoolExecutor(max_workers=args.num_processes) as executor:
                    futures = {
                        executor.submit(
                            run_write_process, i, args.storage_account, args.container,
                            args.blob_size_gb, args.num_blobs, args.write_mode, client_prefix,
                            client, consistent_mode=consistent_mode,
                        ): i
                        for i in range(args.num_processes)
                    }
                    
                    for future in as_completed(futures):
                        proc_id = futures[future]
                        try:
                            result = future.result()
                            write_results.append(result)
                            if result.paths:
                                all_written_paths[proc_id] = result.paths
                            status = "✓" if result.success else "✗"
                            print(f"  Process {proc_id:2d}: {status} {result.throughput_mbps:.0f} MB/s ({result.throughput_gbits:.2f} Gbits/s)")
                            if result.error:
                                print(f"    Error: {result.error}")
                        except Exception as e:
                            print(f"  Process {proc_id:2d}: ✗ Exception: {e}")
            
            write_duration = time.perf_counter() - write_start
            
            # Calculate write aggregate
            successful_writes = [r for r in write_results if r.success]
            total_written = sum(r.bytes_transferred for r in successful_writes)
            write_throughput_mbps = (total_written / MB) / write_duration if write_duration > 0 else 0
            write_throughput_gbits = (write_throughput_mbps * 8) / 1000
            
            print(f"\n{'='*70}")
            print("WRITE RESULTS")
            print(f"{'='*70}")
            print(f"Processes Succeeded: {len(successful_writes)}/{args.num_processes}")
            print(f"Total Data Written:  {total_written / GB:.2f} GB")
            print(f"Wall-Clock Duration: {write_duration:.2f}s")
            print(f"")
            print(f"AGGREGATE WRITE THROUGHPUT:")
            print(f"  {write_throughput_mbps:.0f} MB/s")
            print(f"  {write_throughput_mbps/1024:.2f} GB/s")
            print(f"  {write_throughput_gbits:.2f} Gbits/s")
            print(f"{'='*70}")
            
            # Save paths to file for two-phase benchmarks
            if args.paths_file:
                with open(args.paths_file, 'w', encoding='utf-8') as f:
                    json.dump(all_written_paths, f)
                print(f"Paths saved to {args.paths_file} ({sum(len(p) for p in all_written_paths)} blobs)")
        
        # ==================== OVERWRITE PHASE ====================
        # In consistent mode, re-write the same blob paths with new data to measure overwrite throughput.
        if consistent_mode and not args.skip_write:
            flat_paths = [p for paths in all_written_paths for p in paths]
            if not flat_paths:
                print("\nNo blobs to overwrite (write phase failed)")
            else:
                # Redistribute paths across processes (same as read phase)
                paths_per_process = [[] for _ in range(args.num_processes)]
                for i, path in enumerate(flat_paths):
                    paths_per_process[i % args.num_processes].append(path)
                
                print(f"\n{'='*70}")
                if args.single_process:
                    print(f"OVERWRITE PHASE - Single process (profiling mode)...")
                else:
                    print(f"OVERWRITE PHASE - Launching {args.num_processes} parallel processes...")
                print(f"{'='*70}")
                
                overwrite_start = time.perf_counter()
                overwrite_results: List[ProcessResult] = []
                
                if args.single_process:
                    result = run_write_process(
                        0, args.storage_account, args.container,
                        args.blob_size_gb, args.num_blobs, args.write_mode, client_prefix,
                        client, existing_paths=flat_paths, consistent_mode=consistent_mode,
                    )
                    overwrite_results.append(result)
                    status = "✓" if result.success else "✗"
                    print(f"  {status} {result.throughput_mbps:.0f} MB/s ({result.throughput_gbits:.2f} Gbits/s)")
                    if result.error:
                        print(f"    Error: {result.error}")
                else:
                    with ProcessPoolExecutor(max_workers=args.num_processes) as executor:
                        futures = {}
                        for i in range(args.num_processes):
                            if not paths_per_process[i]:
                                continue
                            futures[executor.submit(
                                run_write_process, i, args.storage_account, args.container,
                                args.blob_size_gb, len(paths_per_process[i]), args.write_mode,
                                client_prefix, client, existing_paths=paths_per_process[i],
                                consistent_mode=consistent_mode,
                            )] = i
                        
                        for future in as_completed(futures):
                            proc_id = futures[future]
                            try:
                                result = future.result()
                                overwrite_results.append(result)
                                status = "✓" if result.success else "✗"
                                print(f"  Process {proc_id:2d}: {status} {result.throughput_mbps:.0f} MB/s ({result.throughput_gbits:.2f} Gbits/s)")
                                if result.error:
                                    print(f"    Error: {result.error}")
                            except Exception as e:
                                print(f"  Process {proc_id:2d}: ✗ Exception: {e}")
                
                overwrite_duration = time.perf_counter() - overwrite_start
                
                successful_overwrites = [r for r in overwrite_results if r.success]
                total_overwritten = sum(r.bytes_transferred for r in successful_overwrites)
                overwrite_throughput_mbps = (total_overwritten / MB) / overwrite_duration if overwrite_duration > 0 else 0
                overwrite_throughput_gbits = (overwrite_throughput_mbps * 8) / 1000
                
                print(f"\n{'='*70}")
                print("OVERWRITE RESULTS")
                print(f"{'='*70}")
                print(f"Processes Succeeded: {len(successful_overwrites)}/{args.num_processes}")
                print(f"Total Data Written:  {total_overwritten / GB:.2f} GB")
                print(f"Wall-Clock Duration: {overwrite_duration:.2f}s")
                print(f"")
                print(f"AGGREGATE OVERWRITE THROUGHPUT:")
                print(f"  {overwrite_throughput_mbps:.0f} MB/s")
                print(f"  {overwrite_throughput_mbps/1024:.2f} GB/s")
                print(f"  {overwrite_throughput_gbits:.2f} Gbits/s")
                print(f"{'='*70}")
        
        # ==================== READ PHASE ====================
        if not args.skip_read:
            # Load paths from file if write phase was skipped (two-phase mode)
            if args.skip_write and args.paths_file and os.path.exists(args.paths_file):
                with open(args.paths_file, 'r', encoding='utf-8') as f:
                    all_written_paths = json.load(f)
                print(f"Paths loaded from {args.paths_file} ({sum(len(p) for p in all_written_paths)} blobs)")
            
            # Flatten paths for reading
            flat_paths = [p for paths in all_written_paths for p in paths]
            
            if not flat_paths:
                print("\nNo blobs to read (write phase skipped or failed)")
            else:
                # Redistribute paths evenly across read processes
                paths_per_process = [[] for _ in range(args.num_processes)]
                for i, path in enumerate(flat_paths):
                    paths_per_process[i % args.num_processes].append(path)
                
                print(f"\n{'='*70}")
                if args.single_process:
                    print(f"READ PHASE - Single process (profiling mode)...")
                else:
                    print(f"READ PHASE - Launching {args.num_processes} parallel processes...")
                print(f"{'='*70}")
                
                read_start = time.perf_counter()
                read_results: List[ProcessResult] = []
                
                if args.single_process:
                    # Run directly in main process for profiling
                    result = run_read_process(0, flat_paths, blob_size, client)
                    read_results.append(result)
                    status = "✓" if result.success else "✗"
                    print(f"  {status} {result.throughput_mbps:.0f} MB/s ({result.throughput_gbits:.2f} Gbits/s)")
                    if result.error:
                        print(f"    Error: {result.error}")
                else:
                    # Multi-process mode for NIC saturation
                    with ProcessPoolExecutor(max_workers=args.num_processes) as executor:
                        futures = {
                            executor.submit(run_read_process, i, paths_per_process[i], blob_size, client): i
                            for i in range(args.num_processes)
                            if paths_per_process[i]  # Only submit if process has paths
                        }
                        
                        for future in as_completed(futures):
                            proc_id = futures[future]
                            try:
                                result = future.result()
                                read_results.append(result)
                                status = "✓" if result.success else "✗"
                                print(f"  Process {proc_id:2d}: {status} {result.throughput_mbps:.0f} MB/s ({result.throughput_gbits:.2f} Gbits/s)")
                                if result.error:
                                    print(f"    Error: {result.error}")
                            except Exception as e:
                                print(f"  Process {proc_id:2d}: ✗ Exception: {e}")
                
                read_duration = time.perf_counter() - read_start
                
                # Calculate read aggregate
                successful_reads = [r for r in read_results if r.success]
                total_read = sum(r.bytes_transferred for r in successful_reads)
                read_throughput_mbps = (total_read / MB) / read_duration if read_duration > 0 else 0
                read_throughput_gbits = (read_throughput_mbps * 8) / 1000
                
                print(f"\n{'='*70}")
                print("READ RESULTS")
                print(f"{'='*70}")
                print(f"Processes Succeeded: {len(successful_reads)}/{args.num_processes}")
                print(f"Total Data Read:     {total_read / GB:.2f} GB")
                print(f"Wall-Clock Duration: {read_duration:.2f}s")
                print(f"")
                print(f"AGGREGATE READ THROUGHPUT:")
                print(f"  {read_throughput_mbps:.0f} MB/s")
                print(f"  {read_throughput_mbps/1024:.2f} GB/s")
                print(f"  {read_throughput_gbits:.2f} Gbits/s")
                print(f"{'='*70}")
        
        # ==================== CLIENT SUMMARY ====================
        print(f"\n{'='*70}")
        print(f"SUMMARY: {client}")
        print(f"{'='*70}")
        
        # Print NIC info
        if nic_speed_gbps > 0:
            print(f"\nNIC ({nic_iface}): {nic_speed_gbps:.0f} Gbits/s")
        
        if not args.skip_write and successful_writes:
            write_throughput_gbps = write_throughput_mbps / 1024
            write_nic_util = (write_throughput_gbits / nic_speed_gbps * 100) if nic_speed_gbps > 0 else 0
            print(f"\nWRITE:")
            print(f"  Total Data:    {total_written / GB:.2f} GB")
            print(f"  Duration:      {write_duration:.2f}s")
            print(f"  Throughput:    {write_throughput_mbps:.0f} MB/s | {write_throughput_gbps:.2f} GB/s")
            print(f"  Network:       {write_throughput_gbits:.2f} Gbits/s", end="")
            if nic_speed_gbps > 0:
                print(f" ({write_nic_util:.1f}% of NIC)")
            else:
                print()
        
        if not args.skip_read and successful_reads:
            read_throughput_gbps = read_throughput_mbps / 1024
            read_nic_util = (read_throughput_gbits / nic_speed_gbps * 100) if nic_speed_gbps > 0 else 0
            print(f"\nREAD:")
            print(f"  Total Data:    {total_read / GB:.2f} GB")
            print(f"  Duration:      {read_duration:.2f}s")
            print(f"  Throughput:    {read_throughput_mbps:.0f} MB/s | {read_throughput_gbps:.2f} GB/s")
            print(f"  Network:       {read_throughput_gbits:.2f} Gbits/s", end="")
            if nic_speed_gbps > 0:
                print(f" ({read_nic_util:.1f}% of NIC)")
            else:
                print()
        
        if consistent_mode and successful_overwrites:
            overwrite_throughput_gbps = overwrite_throughput_mbps / 1024
            overwrite_nic_util = (overwrite_throughput_gbits / nic_speed_gbps * 100) if nic_speed_gbps > 0 else 0
            print(f"\nOVERWRITE:")
            print(f"  Total Data:    {total_overwritten / GB:.2f} GB")
            print(f"  Duration:      {overwrite_duration:.2f}s")
            print(f"  Throughput:    {overwrite_throughput_mbps:.0f} MB/s | {overwrite_throughput_gbps:.2f} GB/s")
            print(f"  Network:       {overwrite_throughput_gbits:.2f} Gbits/s", end="")
            if nic_speed_gbps > 0:
                print(f" ({overwrite_nic_util:.1f}% of NIC)")
            else:
                print()
        
        print(f"{'='*70}")
        
        # Store results for comparison
        client_results[client] = {
            "write_bytes": total_written,
            "write_duration_seconds": write_duration,
            "write_mbps": write_throughput_mbps,
            "read_bytes": total_read,
            "read_duration_seconds": read_duration,
            "read_mbps": read_throughput_mbps,
            "overwrite_bytes": total_overwritten,
            "overwrite_duration_seconds": overwrite_duration,
            "overwrite_mbps": overwrite_throughput_mbps,
        }
        
        # Cleanup
        if not args.no_cleanup:
            flat_paths = [p for paths in all_written_paths for p in paths]
            if flat_paths:
                cleanup_paths(flat_paths)
    
    # ==================== COMPARISON SUMMARY ====================
    if len(clients) > 1:
        print(f"\n{'#'*70}")
        print("# COMPARISON SUMMARY")
        print(f"{'#'*70}")
        
        # Find baseline (blobfile if present, else first client)
        baseline = "blobfile" if "blobfile" in clients else clients[0]
        baseline_write = client_results[baseline]["write_mbps"]
        baseline_read = client_results[baseline]["read_mbps"]
        
        print(f"\n{'Client':<20} {'Write MB/s':>12} {'Write Speedup':>14} {'Read MB/s':>12} {'Read Speedup':>14}")
        print("-" * 74)
        for c in clients:
            r = client_results[c]
            write_speedup = r["write_mbps"] / baseline_write if baseline_write > 0 else 0
            read_speedup = r["read_mbps"] / baseline_read if baseline_read > 0 else 0
            speedup_w = f"{write_speedup:.2f}x" if c != baseline else "(baseline)"
            speedup_r = f"{read_speedup:.2f}x" if c != baseline else "(baseline)"
            print(f"{c:<20} {r['write_mbps']:>12.0f} {speedup_w:>14} {r['read_mbps']:>12.0f} {speedup_r:>14}")
        
        print(f"{'='*70}")
    
    # ==================== JSON OUTPUT ====================
    if args.output:
        output_data = {
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "config": {
                "storage_account": args.storage_account,
                "container": args.container,
                "blob_size_gb": args.blob_size_gb,
                "num_blobs": args.num_blobs,
                "num_processes": args.num_processes,
                "write_mode": args.write_mode,
                "node_id": node_id,
                "clients": clients,
            },
            "results": client_results,
        }
        output_dir = os.path.dirname(args.output)
        os.makedirs(output_dir if output_dir else ".", exist_ok=True)
        with open(args.output, "w") as f:
            json.dump(output_data, f, indent=2)
        print(f"\nResults saved to: {args.output}")


if __name__ == "__main__":
    main()
