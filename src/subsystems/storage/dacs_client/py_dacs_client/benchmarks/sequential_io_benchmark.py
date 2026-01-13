#!/usr/bin/env python3
# --------------------------------------------------------------------------------------------------------------------
# Sequential Read/Write Benchmark
# Compares blobfile vs DACS Client for sequential I/O operations
# --------------------------------------------------------------------------------------------------------------------

import argparse
import ctypes
import json
import logging
import os
import sys
import tempfile
import time
import uuid
from dataclasses import dataclass, asdict
from datetime import datetime, timezone
from typing import Optional, List

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
logger = logging.getLogger(__name__)

# Try to import blobfile
try:
    import blobfile as bf

    BLOBFILE_AVAILABLE = True
except ImportError:
    logger.warning("blobfile not available - install with: pip install blobfile")
    BLOBFILE_AVAILABLE = False

# Try to import DACS client
try:
    from py_dacs_client.dacs_blobfile import BlobFile, WriteMode, shutdown as dacs_shutdown
    from py_dacs_client.dacs_utils import alloc_buffer, release_buffer

    DACS_AVAILABLE = True
except ImportError as e:
    logger.warning(f"DACS client not available: {e}")
    DACS_AVAILABLE = False


# Constants
GB = 1024 * 1024 * 1024
MB = 1024 * 1024
DEFAULT_BLOB_SIZE = 1 * GB  # 1 GB per blob
DEFAULT_NUM_BLOBS = 10  # 10 blobs = 10 GB total
DEFAULT_CHUNK_SIZE = 32 * MB  # 32 MB chunks for sequential I/O


@dataclass
class BenchmarkResult:
    """Result of a benchmark run."""

    client: str
    operation: str
    num_blobs: int
    blob_size_gb: float
    total_size_gb: float
    chunk_size_mb: float
    duration_seconds: float
    throughput_gbps: float
    throughput_mbps: float
    success: bool
    error: Optional[str] = None
    timestamp: str = None

    def __post_init__(self):
        if self.timestamp is None:
            self.timestamp = datetime.now(timezone.utc).isoformat()


def generate_test_data(size: int) -> bytes:
    """Generate random test data."""
    logger.info(f"Generating {size / MB:.0f} MB of test data...")
    return os.urandom(size)


def get_blob_path(storage_account: str, container: str, prefix: str, client: str, op: str, blob_index: int = 0) -> str:
    """Generate unique blob path with nested directory structure."""
    unique_id = uuid.uuid4().hex[:8]
    blob_name = f"{prefix}/{client}_{op}_{blob_index:03d}_{unique_id}.bin"

    # Use long format for dacs, short format for blobfile
    if client.startswith("dacs"):
        # Long format: az://account.blob.core.windows.net/container/path
        return f"az://{storage_account}.blob.core.windows.net/{container}/{blob_name}"
    else:
        # Short format for blobfile: az://account/container/path
        return f"az://{storage_account}/{container}/{blob_name}"


# =============================================================================
# BLOBFILE Operations
# =============================================================================


def blobfile_sequential_write(path: str, data: bytes, chunk_size: int) -> float:
    """Write data sequentially using blobfile."""
    logger.info(f"[blobfile] Writing {len(data) / GB:.2f} GB to {path}")

    start = time.perf_counter()
    with bf.BlobFile(path, "wb") as f:
        f.write(data)

    duration = time.perf_counter() - start
    speed = (len(data) / MB) / duration if duration > 0 else 0
    logger.info(f"[blobfile] Write completed in {duration:.2f}s ({speed:.1f} MB/s)")
    return duration


def blobfile_sequential_read(path: str, file_size: int, chunk_size: int) -> tuple[float, bytes]:
    """Read data sequentially using blobfile."""
    logger.info(f"[blobfile] Reading {file_size / GB:.2f} GB from {path}")

    start = time.perf_counter()
    with bf.BlobFile(path, "rb") as f:
        data = f.read()

    duration = time.perf_counter() - start
    speed = (len(data) / MB) / duration if duration > 0 else 0
    logger.info(f"[blobfile] Read completed in {duration:.2f}s, got {len(data) / GB:.2f} GB ({speed:.1f} MB/s)")
    return duration, data


# =============================================================================
# DACS Operations
# =============================================================================


def dacs_sequential_write(path: str, data: bytes, chunk_size: int, write_mode: WriteMode) -> float:
    """Write data sequentially using DACS client."""
    logger.info(f"[dacs] Writing {len(data) / GB:.2f} GB to {path}")

    start = time.perf_counter()
    with BlobFile(path, "wb", write_mode=write_mode) as f:
        f.write(data)

    duration = time.perf_counter() - start
    speed = (len(data) / MB) / duration if duration > 0 else 0
    logger.info(f"[dacs] Write completed in {duration:.2f}s ({speed:.1f} MB/s)")
    return duration


def dacs_sequential_write_zero_copy(path: str, data: bytes, chunk_size: int, write_mode: WriteMode) -> float:
    """Write data using DACS with zero-copy buffer."""
    logger.info(f"[dacs-zc] Writing {len(data) / GB:.2f} GB to {path} (zero-copy)")

    logger.info(f"  [dacs-zc] Allocating buffer...")
    buffer = alloc_buffer(len(data))
    logger.info(f"  [dacs-zc] Copying data to buffer...")
    # ctypes.memmove handles bytes objects directly via buffer protocol
    ctypes.memmove(buffer, data, len(data))

    try:
        logger.info(f"  [dacs-zc] Starting write operation...")
        start = time.perf_counter()
        with BlobFile(path, "wb", write_mode=write_mode) as f:
            f.write(buffer=buffer, size=len(data))
        duration = time.perf_counter() - start
    finally:
        release_buffer(buffer)

    speed = (len(data) / MB) / duration if duration > 0 else 0
    logger.info(f"[dacs-zc] Write completed in {duration:.2f}s ({speed:.1f} MB/s)")
    return duration


def dacs_sequential_read(path: str, file_size: int, chunk_size: int) -> tuple[float, bytes]:
    """Read data sequentially using DACS client (simple read, no pre-allocated buffer)."""
    logger.info(f"[dacs] Reading {file_size / GB:.2f} GB from {path}")

    start = time.perf_counter()
    with BlobFile(path, "rb") as f:
        data = f.read(size=file_size)
    duration = time.perf_counter() - start

    speed = (len(data) / MB) / duration if duration > 0 else 0
    logger.info(f"[dacs] Read completed in {duration:.2f}s, got {len(data) / GB:.2f} GB ({speed:.1f} MB/s)")
    return duration, data


def dacs_sequential_read_zero_copy(path: str, file_size: int, chunk_size: int) -> tuple[float, bytes]:
    """Read data using DACS with zero-copy buffer."""
    logger.info(f"[dacs-zc] Reading {file_size / GB:.2f} GB from {path} (zero-copy)")

    logger.info(f"  [dacs-zc] Allocating buffer...")
    buffer = alloc_buffer(file_size)

    try:
        logger.info(f"  [dacs-zc] Starting read operation...")
        start = time.perf_counter()
        with BlobFile(path, "rb") as f:
            mv = f.read(size=file_size, buffer=buffer)
            # Copy to bytes for verification
            logger.info(f"  [dacs-zc] Copying to bytes for verification...")
            data = bytes(mv)
        duration = time.perf_counter() - start
    finally:
        release_buffer(buffer)

    logger.info(f"[dacs-zc] Read completed in {duration:.2f}s, got {len(data) / GB:.2f} GB")
    return duration, data


# =============================================================================
# Benchmark Runner
# =============================================================================


def run_benchmark(
    storage_account: str,
    container: str,
    blob_size: int = DEFAULT_BLOB_SIZE,
    num_blobs: int = DEFAULT_NUM_BLOBS,
    chunk_size: int = DEFAULT_CHUNK_SIZE,
    clients: list[str] = None,
    write_mode: str = "write_sync",
    skip_write: bool = False,
    skip_read: bool = False,
    verify_data: bool = True,
    cleanup: bool = True,
) -> list[BenchmarkResult]:
    """Run the sequential read/write benchmark with multiple blobs."""

    if clients is None:
        clients = []
        if BLOBFILE_AVAILABLE:
            clients.append("blobfile")
        if DACS_AVAILABLE:
            clients.extend(["dacs", "dacs_zero_copy"])

    if not clients:
        logger.error("No clients available for benchmarking!")
        return []

    total_size = blob_size * num_blobs
    results = []
    # Use nested directory structure
    prefix = f"dacs_tests/benchmark/{datetime.now(timezone.utc).strftime('%Y%m%d_%H%M%S')}"

    # Map write mode string to enum
    write_mode_enum = WriteMode.WRITE_SYNC
    if DACS_AVAILABLE:
        mode_map = {
            "write_sync": WriteMode.WRITE_SYNC,
            "write_cache_only": WriteMode.WRITE_CACHE_ONLY,
            "write_blob_only": WriteMode.WRITE_BLOB_ONLY,
            "write_cache_blob_async": WriteMode.WRITE_CACHE_BLOB_ASYNC,
        }
        write_mode_enum = mode_map.get(write_mode, WriteMode.WRITE_SYNC)

    print("\n" + "=" * 70)
    print("SEQUENTIAL READ/WRITE BENCHMARK (Multiple Blobs)")
    print("=" * 70)
    print(f"Blob Size:    {blob_size / GB:.2f} GB")
    print(f"Num Blobs:    {num_blobs}")
    print(f"Total Size:   {total_size / GB:.2f} GB")
    print(f"Chunk Size:   {chunk_size / MB:.0f} MB")
    print(f"Clients:      {', '.join(clients)}")
    print(f"Write Mode:   {write_mode}")
    print(f"Verify Data:  {verify_data}")
    print("=" * 70 + "\n")

    # Generate test data once (1GB per blob)
    logger.info("Preparing test data...")
    test_data = generate_test_data(blob_size)
    logger.info(f"Test data ready: {len(test_data) / GB:.2f} GB (reused for each blob)")

    # Track written paths for read tests: client -> list of paths
    written_paths: dict[str, List[str]] = {}

    # ==========================================================================
    # WRITE BENCHMARKS
    # ==========================================================================
    if not skip_write:
        print("\n" + "-" * 70)
        print(f"WRITE BENCHMARKS ({num_blobs} blobs x {blob_size / GB:.0f} GB = {total_size / GB:.0f} GB total)")
        print("-" * 70)

        for client in clients:
            client_paths = []
            total_duration = 0.0

            try:
                for blob_idx in range(num_blobs):
                    path = get_blob_path(storage_account, container, prefix, client, "write", blob_idx)

                    logger.info(f"[{client}] Writing blob {blob_idx + 1}/{num_blobs}...")

                    if client == "blobfile" and BLOBFILE_AVAILABLE:
                        duration = blobfile_sequential_write(path, test_data, chunk_size)
                    elif client == "dacs" and DACS_AVAILABLE:
                        duration = dacs_sequential_write(path, test_data, chunk_size, write_mode_enum)
                    elif client == "dacs_zero_copy" and DACS_AVAILABLE:
                        duration = dacs_sequential_write_zero_copy(path, test_data, chunk_size, write_mode_enum)
                    else:
                        logger.warning(f"Client {client} not available, skipping")
                        break

                    total_duration += duration
                    client_paths.append(path)
                    logger.info(f"[{client}] Blob {blob_idx + 1}/{num_blobs} completed in {duration:.2f}s")

                if not client_paths:
                    continue

                # Calculate aggregate throughput
                throughput_gbps = (total_size / GB) / total_duration if total_duration > 0 else 0
                throughput_mbps = (total_size / MB) / total_duration if total_duration > 0 else 0

                result = BenchmarkResult(
                    client=client,
                    operation="write",
                    num_blobs=num_blobs,
                    blob_size_gb=blob_size / GB,
                    total_size_gb=total_size / GB,
                    chunk_size_mb=chunk_size / MB,
                    duration_seconds=total_duration,
                    throughput_gbps=throughput_gbps,
                    throughput_mbps=throughput_mbps,
                    success=True,
                )
                results.append(result)
                written_paths[client] = client_paths

                print(
                    f"  {client:<20} {total_duration:>8.2f}s  {throughput_gbps:>6.2f} GB/s  {throughput_mbps:>8.0f} MB/s"
                )

            except Exception as e:
                logger.error(f"Write failed for {client}: {e}")
                results.append(
                    BenchmarkResult(
                        client=client,
                        operation="write",
                        num_blobs=num_blobs,
                        blob_size_gb=blob_size / GB,
                        total_size_gb=total_size / GB,
                        chunk_size_mb=chunk_size / MB,
                        duration_seconds=0,
                        throughput_gbps=0,
                        throughput_mbps=0,
                        success=False,
                        error=str(e),
                    )
                )

    # ==========================================================================
    # READ BENCHMARKS
    # ==========================================================================
    if not skip_read:
        print("\n" + "-" * 70)
        print(f"READ BENCHMARKS ({num_blobs} blobs x {blob_size / GB:.0f} GB = {total_size / GB:.0f} GB total)")
        print("-" * 70)

        for client in clients:
            # Use the paths written by this client, or skip if not available
            if client not in written_paths:
                logger.warning(f"No written files for {client}, skipping read")
                continue

            client_paths = written_paths[client]
            total_duration = 0.0
            all_valid = True

            try:
                for blob_idx, path in enumerate(client_paths):
                    logger.info(f"[{client}] Reading blob {blob_idx + 1}/{num_blobs}...")

                    if client == "blobfile" and BLOBFILE_AVAILABLE:
                        duration, read_data = blobfile_sequential_read(path, blob_size, chunk_size)
                    elif client == "dacs" and DACS_AVAILABLE:
                        duration, read_data = dacs_sequential_read(path, blob_size, chunk_size)
                    elif client == "dacs_zero_copy" and DACS_AVAILABLE:
                        duration, read_data = dacs_sequential_read_zero_copy(path, blob_size, chunk_size)
                    else:
                        continue

                    total_duration += duration

                    # Verify data integrity
                    if verify_data:
                        if read_data != test_data:
                            logger.error(f"DATA MISMATCH for {client} blob {blob_idx}!")
                            all_valid = False
                        else:
                            logger.info(f"Data verification passed for {client} blob {blob_idx + 1}/{num_blobs}")

                    logger.info(f"[{client}] Blob {blob_idx + 1}/{num_blobs} read completed in {duration:.2f}s")

                # Calculate aggregate throughput
                throughput_gbps = (total_size / GB) / total_duration if total_duration > 0 else 0
                throughput_mbps = (total_size / MB) / total_duration if total_duration > 0 else 0

                result = BenchmarkResult(
                    client=client,
                    operation="read",
                    num_blobs=num_blobs,
                    blob_size_gb=blob_size / GB,
                    total_size_gb=total_size / GB,
                    chunk_size_mb=chunk_size / MB,
                    duration_seconds=total_duration,
                    throughput_gbps=throughput_gbps,
                    throughput_mbps=throughput_mbps,
                    success=all_valid,
                    error=None if all_valid else "Data verification failed",
                )
                results.append(result)

                status = "✓" if all_valid else "✗"
                print(
                    f"  {client:<20} {total_duration:>8.2f}s  {throughput_gbps:>6.2f} GB/s  {throughput_mbps:>8.0f} MB/s  {status}"
                )

            except Exception as e:
                logger.error(f"Read failed for {client}: {e}")
                results.append(
                    BenchmarkResult(
                        client=client,
                        operation="read",
                        num_blobs=num_blobs,
                        blob_size_gb=blob_size / GB,
                        total_size_gb=total_size / GB,
                        chunk_size_mb=chunk_size / MB,
                        duration_seconds=0,
                        throughput_gbps=0,
                        throughput_mbps=0,
                        success=False,
                        error=str(e),
                    )
                )

    # ==========================================================================
    # SUMMARY
    # ==========================================================================
    print("\n" + "=" * 70)
    print(f"SUMMARY ({num_blobs} blobs x {blob_size / GB:.0f} GB = {total_size / GB:.0f} GB total)")
    print("=" * 70)

    # Group by operation
    for op in ["write", "read"]:
        op_results = [r for r in results if r.operation == op and r.success]
        if not op_results:
            continue

        print(f"\n{op.upper()}:")
        print(f"  {'Client':<22} {'Duration':>10} {'GB/s':>10} {'MB/s':>10}   {'Speedup':<20}")
        print(f"  {'-'*22} {'-'*10} {'-'*10} {'-'*10}   {'-'*20}")

        # Find baseline (blobfile)
        baseline = next((r for r in op_results if r.client == "blobfile"), None)

        for r in op_results:
            speedup = "-"
            if baseline and r.client != "blobfile" and baseline.duration_seconds > 0:
                ratio = baseline.duration_seconds / r.duration_seconds
                speedup = f"{ratio:.2f}x vs blobfile"

            print(
                f"  {r.client:<22} {r.duration_seconds:>9.2f}s {r.throughput_gbps:>10.2f} {r.throughput_mbps:>10.0f}   {speedup:<20}"
            )

    # Overall summary table
    print("\n" + "-" * 70)
    print("THROUGHPUT COMPARISON (MB/s)")
    print("-" * 70)

    write_results = {r.client: r for r in results if r.operation == "write" and r.success}
    read_results = {r.client: r for r in results if r.operation == "read" and r.success}
    all_clients = sorted(set(write_results.keys()) | set(read_results.keys()))

    print(f"  {'Client':<22} {'Write (MB/s)':>15} {'Read (MB/s)':>15}")
    print(f"  {'-'*22} {'-'*15} {'-'*15}")

    for client in all_clients:
        w_mbps = write_results[client].throughput_mbps if client in write_results else 0
        r_mbps = read_results[client].throughput_mbps if client in read_results else 0

        w_str = f"{w_mbps:>15.0f}" if w_mbps > 0 else "            N/A"
        r_str = f"{r_mbps:>15.0f}" if r_mbps > 0 else "            N/A"

        print(f"  {client:<22} {w_str} {r_str}")

    print("\n" + "=" * 70)

    # ==========================================================================
    # CLEANUP
    # ==========================================================================
    if cleanup and written_paths:
        print("\nCleaning up test files...")
        for client, paths in written_paths.items():
            for path in paths:
                try:
                    # Convert dacs long format to blobfile short format for cleanup
                    # az://account.blob.core.windows.net/container/path -> az://account/container/path
                    cleanup_path = path
                    if ".blob.core.windows.net" in path:
                        # Extract components from long format
                        # Format: az://account.blob.core.windows.net/container/path
                        parts = path.replace("az://", "").split("/", 2)
                        if len(parts) >= 2:
                            account = parts[0].replace(".blob.core.windows.net", "")
                            container_name = parts[1]
                            blob_path = parts[2] if len(parts) > 2 else ""
                            cleanup_path = f"az://{account}/{container_name}/{blob_path}"
                        else:
                            logger.warning(f"Unexpected path format, using original: {path}")

                    if BLOBFILE_AVAILABLE:
                        if bf.exists(cleanup_path):
                            bf.remove(cleanup_path)
                            logger.info(f"Removed: {cleanup_path}")
                        else:
                            logger.warning(f"Blob not found (may already be deleted): {cleanup_path}")
                except Exception as e:
                    logger.warning(f"Failed to remove {path}: {e}")
        print("Cleanup completed.")

    return results


def save_results(results: list[BenchmarkResult], output_path: str):
    """Save results to JSON file."""
    os.makedirs(os.path.dirname(output_path) if os.path.dirname(output_path) else ".", exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as f:
        json.dump([asdict(r) for r in results], f, indent=2)
    logger.info(f"Results saved to: {output_path}")


def parse_args():
    parser = argparse.ArgumentParser(
        description="Sequential Read/Write Benchmark (Multiple Blobs)",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )

    parser.add_argument(
        "--storage-account",
        default="storageinterceptionpr1",
        help="Azure storage account name",
    )
    parser.add_argument(
        "--container",
        default="dataset",
        help="Azure blob container name",
    )
    parser.add_argument(
        "--blob-size-gb",
        type=float,
        default=DEFAULT_BLOB_SIZE / GB,
        help="Size of each blob in GB",
    )
    parser.add_argument(
        "--num-blobs",
        type=int,
        default=DEFAULT_NUM_BLOBS,
        help="Number of blobs to read/write",
    )
    parser.add_argument(
        "--chunk-size-mb",
        type=int,
        default=DEFAULT_CHUNK_SIZE // MB,
        help="Chunk size in MB for sequential I/O",
    )
    parser.add_argument(
        "--clients",
        type=str,
        default="blobfile,dacs,dacs_zero_copy",
        help="Comma-separated list of clients to benchmark",
    )
    parser.add_argument(
        "--write-mode",
        choices=["write_sync", "write_cache_only", "write_blob_only", "write_cache_blob_async"],
        default="write_sync",
        help="DACS write mode",
    )
    parser.add_argument(
        "--skip-write",
        action="store_true",
        help="Skip write benchmark",
    )
    parser.add_argument(
        "--skip-read",
        action="store_true",
        help="Skip read benchmark",
    )
    parser.add_argument(
        "--no-verify",
        action="store_true",
        help="Skip data verification after read",
    )
    parser.add_argument(
        "--no-cleanup",
        action="store_true",
        help="Skip cleanup of test files after benchmark",
    )
    parser.add_argument(
        "--output",
        type=str,
        default="./benchmark_results/sequential_benchmark_results.json",
        help="Output file path for results",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Enable verbose logging",
    )
    parser.add_argument(
        "--temp-dir",
        type=str,
        default=None,
        help="Temp directory for blobfile staging (default: system temp). Use a path with enough space for large files.",
    )

    return parser.parse_args()


def main():
    args = parse_args()

    if args.verbose:
        logging.getLogger().setLevel(logging.DEBUG)

    # Set custom temp directory for blobfile staging (avoids /tmp space issues)
    if args.temp_dir:
        os.makedirs(args.temp_dir, exist_ok=True)
        os.environ["TMPDIR"] = args.temp_dir
        tempfile.tempdir = args.temp_dir
        logger.info(f"Using temp directory: {args.temp_dir}")

    # Parse clients
    clients = [c.strip() for c in args.clients.split(",") if c.strip()]

    # Run benchmark
    results = run_benchmark(
        storage_account=args.storage_account,
        container=args.container,
        blob_size=int(args.blob_size_gb * GB),
        num_blobs=args.num_blobs,
        chunk_size=args.chunk_size_mb * MB,
        clients=clients,
        write_mode=args.write_mode,
        skip_write=args.skip_write,
        skip_read=args.skip_read,
        verify_data=not args.no_verify,
        cleanup=not args.no_cleanup,
    )

    # Save results
    save_results(results, args.output)

    # Cleanup DACS client
    if DACS_AVAILABLE:
        try:
            dacs_shutdown()
        except Exception as e:
            logger.warning(f"Error during DACS shutdown: {e}")

    # Exit with error if any test failed
    failed = [r for r in results if not r.success]
    if failed:
        logger.error(f"{len(failed)} test(s) failed")
        sys.exit(1)


if __name__ == "__main__":
    main()
