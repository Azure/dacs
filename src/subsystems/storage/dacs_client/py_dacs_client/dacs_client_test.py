# --------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# Sample code to integrate and test, DACS Client
# --------------------------------------------------------------------------------------------------------------------
import argparse
import asyncio
import os
import random
import sys
import time
from ctypes import memset  # Added for fast buffer initialization
from py_dacs_client.dacs_utils import alloc_buffer, logger, release_buffer, BoundedBufferManager, get_memory_limit
from py_dacs_client.dacs_client import DACSCacheClient, DACSCacheFileHandle
from mpi4py import MPI
import socket


def run_all_tasks(tasks: list[asyncio.Task]) -> None:
    async def _run_all():
        await asyncio.gather(*tasks)

    asyncio.run(_run_all())


# Sample code for reference and also to be used in the test
def perf_test_read(nBlobs: int, azBlobDirPath: str, blobNamePattern: str, blobSizeToBeRead: int) -> None:
    """
    Perform a performance test by reading blobs from a given directory.
    Args:
        nBlobs (int): Number of blobs to read.
        azBlobDirPath (str): Directory name containing the blobs.
        blobNamePattern (str): Pattern to match blob names.
    """
    # Check if the azBlobDirPath is set
    if not azBlobDirPath:
        raise Exception("azBlobDirPath is not set. Please set it to run the test.")
    # Check if the azBlobDirPath is a valid path
    if not azBlobDirPath.startswith("az://"):
        raise Exception("azBlobDirPath is not a valid path. Please set it to run the test.")

    # Initialize the DACSCacheClient
    dacs_client_instance = DACSCacheClient()

    # Initialize BoundedBufferManager to limit memory usage
    memory_limit = get_memory_limit()
    buffer_manager = BoundedBufferManager(max_memory_limit=int(memory_limit * 0.8))
    logger.info(f"BoundedBufferManager initialized with {int(memory_limit * 0.8) / (1024*1024):.0f} MB limit")

    # TODO: also test list blobs api to get the blob names matching the pattern
    # For now, we will construct the blob names based on the pattern
    blob_dict = {}
    total_bytes_read = 0
    start = time.time()
    blob_ids = [os.path.join(azBlobDirPath, f"testWrite_{i}_{blobNamePattern}") for i in range(nBlobs)]
    blob_dict = {}
    for i in range(nBlobs):
        blob_dict[i] = {"name": f"testWrite_{i}_{blobNamePattern}", "size": blobSizeToBeRead}

    # Read the blobs
    read_tasks = []

    async def read_blob_async(dacs_client_instance: DACSCacheClient, blob_path: str, blob_size: int) -> None:

        nonlocal total_bytes_read
        # Allocate buffer for reading via BoundedBufferManager
        buffer = await buffer_manager.acquire_buffer(blob_size)
        if not buffer:
            raise MemoryError("Failed to allocate buffer for reading blob")
        try:
            logger.info(f"Reading blob {blob_path} with size {blob_size} bytes")
            size_read = await dacs_client_instance.read_blob(blob_path, buffer, blob_size, 0)
            if size_read != blob_size:
                logger.error(f"Error reading blob {blob_path}, expected size: {blob_size}, read size: {size_read}")
                raise IOError(f"Error reading blob {blob_path}, expected size: {blob_size}, read size: {size_read}")
            total_bytes_read += size_read
        finally:
            # Release the buffer regardless of success or failure
            await buffer_manager.release_buffer(buffer)

    for i in range(nBlobs):
        blob_path = os.path.join(azBlobDirPath, blob_dict[i]["name"])
        blob_size = blob_dict[i]["size"]
        read_tasks.append(read_blob_async(dacs_client_instance, blob_path, blob_size))

    run_all_tasks(read_tasks)
    end_time = time.time()
    downloadSpeedMBps = (total_bytes_read / (end_time - start)) / (1024 * 1024)
    logger.info(
        f"DACS_Cache_Client_Test:Read {total_bytes_read} bytes ({total_bytes_read /(1024*1024*1024)} GBs)"
        f" in {end_time - start:.2f} seconds at {downloadSpeedMBps / 128} Gbps"
    )

    # Shutdown the client
    asyncio.run(dacs_client_instance.shutdown())


def perf_test_write(nBlobs: int, azBlobDirPath: str, blobNamePattern: str, blobSize: int, write_mode: None):
    """
    Perform a performance test by writing blobs to a given directory.
    Returns tuple: (total_bytes_written, elapsed_seconds, uploadSpeedMBps)
    """
    # TODO: very hacky test code, need following improvements
    # 1. write in chunks to open file handle, write in chunks parallely and then close the handle
    # 2. memory usage is now bounded via BoundedBufferManager
    # 3. read back the written file and validate contents

    # Check if the azBlobDirPath is set
    if not azBlobDirPath:
        raise Exception("azBlobDirPath is not set. Please set it to run the test.")
    # Check if the azBlobDirPath is a valid path
    if not azBlobDirPath.startswith("az://"):
        raise Exception("azBlobDirPath is not a valid path. Please set it to run the test.")

    # Initialize the DACSCacheClient
    dacs_client_instance = DACSCacheClient(write_mode=write_mode)

    # Initialize BoundedBufferManager to limit memory usage during writes
    memory_limit = get_memory_limit()
    buffer_manager = BoundedBufferManager(max_memory_limit=int(memory_limit * 0.8))
    logger.info(f"BoundedBufferManager initialized with {int(memory_limit * 0.8) / (1024*1024):.0f} MB limit")

    # Generate write buffers for each blob using BoundedBufferManager
    start_time = time.time()

    async def _alloc_write_buffers():
        buffers = []
        for _ in range(nBlobs):
            buf = await buffer_manager.acquire_buffer(blobSize)
            if not buf:
                raise MemoryError("Failed to allocate buffer for writing blob")
            # Fast fill buffer with a repeating byte pattern (0xAB) and make it unique per blob
            memset(buf, 0xAB + random.randint(0, 255), blobSize)
            buffers.append(buf)
        return buffers

    write_buffers = asyncio.run(_alloc_write_buffers())
    elapsed_time = time.time() - start_time
    logger.info(
        f"Allocated and initialized {nBlobs} write buffers of size {blobSize} bytes each in {elapsed_time:.2f} seconds"
    )

    # Open file handles for each blob
    start_time = time.time()
    write_file_handles = [None] * nBlobs
    open_tasks = []
    for i in range(nBlobs):
        blob_name = f"testWrite_{i}_{blobNamePattern}"
        blob_path = os.path.join(azBlobDirPath, blob_name)

        async def open_file_handle_async(
            dacs_client_instance: DACSCacheClient,
            blob_path: str,
            index: int,
        ) -> None:
            file_handle = await dacs_client_instance.get_file_handle(
                blob_path, os.O_CREAT | os.O_WRONLY | os.O_EXCL
            )
            write_file_handles[index] = file_handle
            logger.info(f"Opened file handle for blob {blob_path}")

        open_tasks.append(open_file_handle_async(dacs_client_instance, blob_path, i))

    run_all_tasks(open_tasks)
    open_handles_time = time.time() - start_time
    logger.info(f"Opened {nBlobs} file handles in {open_handles_time:.2f} seconds")

    write_tasks = []
    start = time.time()
    total_bytes_written = 0
    for i in range(nBlobs):
        # Construct the full path to the blob
        blob_name = f"testWrite_{i}_{blobNamePattern}"
        blob_path = os.path.join(azBlobDirPath, blob_name)

        # Get buffer for writing
        buffer = write_buffers[i]
        logger.info(f"Writing blob {blob_path} with size {blobSize} bytes")

        async def write_blob_async(
            dacs_client_instance: DACSCacheClient,
            file_handle: DACSCacheFileHandle,
            buffer: memoryview,
            blobSize: int,
        ) -> None:
            nonlocal total_bytes_written
            bytes_written = await file_handle.write(buffer, blobSize, 0)
            if bytes_written != blobSize:
                logger.error(
                    f"Error writing blob {blob_path}, expected size: {blobSize}, written size: {bytes_written}"
                )
                raise IOError(
                    f"Error writing blob {blob_path}, expected size: {blobSize}, written size: {bytes_written}"
                )
            total_bytes_written += bytes_written
            # Release the buffer after closing the file handle
            try:
                await file_handle.__aexit__(None, None, None)
            except Exception as e:
                # Bubble up the error so the caller can decide.
                logger.error(f"Error closing file handle for blob {blob_path}: {e}")
                raise
            finally:
                await buffer_manager.release_buffer(buffer)
            logger.info(f"Completed writing blob {blob_path}, total size: {bytes_written} bytes")

        write_tasks.append(write_blob_async(dacs_client_instance, write_file_handles[i], buffer, blobSize))

    # If any file handle close fails, stop and return to caller
    try:
        run_all_tasks(write_tasks)
    except Exception as e:
        logger.fatal(f"Write tasks failed: {e}")
        return None
    end_time = time.time()
    write_and_close_elapsed = end_time - start
    uploadSpeedMBps = (
        (total_bytes_written / write_and_close_elapsed) / (1024 * 1024) if write_and_close_elapsed > 0 else 0
    )
    logger.info(
        f"DACS_Cache_Client_Test:Wrote {nBlobs} blobs with total {total_bytes_written} bytes"
        f" ({total_bytes_written /(1024*1024*1024)} GBs) in {write_and_close_elapsed:.2f} seconds at {uploadSpeedMBps / 128} Gbps"
    )
    return None


# main entrypoint
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Run DACS Client performance test.")
    parser.add_argument("--nBlobs", type=int, default=1, help="Number of blobs to read")
    parser.add_argument(
        "--azBlobDirPath",
        type=str,
        default="az://storageinterceptionpr1.blob.core.windows.net/dataset/writePerfTest",
        help="Blob directory path to be tested",
    )
    parser.add_argument(
        "--blobNamePattern", type=str, default="1000G", help="Specific file name to be used for testing"
    )
    parser.add_argument("--blobSizeToBeRead", type=int, default=0, help="Size of the blob to be read")
    parser.add_argument("--readOnly", action="store_true", help="Skip write test and only perform read test")
    parser.add_argument("--writeMode", type=str, default="write_sync", help="Write mode for the DACS Client (e.g., 'write_disabled', 'write_cache_only', 'write_blob_only', 'write_cache_blob_async', 'write_sync')")

    args = parser.parse_args()

    logger.info(
        f"\n\nDACS_Cache_Client_Test:Starting Perf test with nBlobs:{args.nBlobs} azBlobDirPath:{args.azBlobDirPath} blobNamePattern:{args.blobNamePattern}\n\n"
    )

    comm = MPI.COMM_WORLD
    rank = comm.Get_rank()
    host = socket.gethostname()

    time.sleep(rank * 3) # staggered start to reduce contention
    logger.info(f"Ready to run test on host: {host}")

    # Logical synchronization
    comm.Barrier()

    test_start_time = MPI.Wtime()
    logger.info(f"Starting test on {host} at time {test_start_time}")

    if not args.readOnly:
        try: 
            perf_test_write(args.nBlobs, args.azBlobDirPath, args.blobNamePattern, args.blobSizeToBeRead, args.writeMode)
        except Exception as e:
            logger.fatal(f"Write test failed: {e}")
            sys.exit(1)

        # sleep for a while to allow async uploads to complete if any
        async_upload_wait_time = os.environ.get("ASYNC_UPLOAD_WAIT_TIME_SECONDS", "20")
        time.sleep(int(async_upload_wait_time))

    logger.info(
        f"\n\nDACS_Cache_Client_Test:Starting Read back test with nBlobs:{args.nBlobs} azBlobDirPath:{args.azBlobDirPath} blobNamePattern:{args.blobNamePattern}\n\n"
    )
    perf_test_read(args.nBlobs, args.azBlobDirPath, args.blobNamePattern, args.blobSizeToBeRead)

    test_end_time = MPI.Wtime()
    logger.info(f"Completed test on {host} at time {test_end_time} elapsed seconds: {test_end_time - test_start_time}\n\n")
