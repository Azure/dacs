# --------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# Sample code to integrate and test, DACS Cache Client
# --------------------------------------------------------------------------------------------------------------------
import argparse
import asyncio
import os
import sys
import time
from py_dacs_cache_client.dacs_cache_utils import get_buffer_to_read, logger, release_buffer
from py_dacs_cache_client.dacs_cache_client import DACSCacheClient

# Sample code for reference and also to be used in the test
def perf_test(nBlobs: int, azBlobDirPath: str, blobNamePattern: str, blobSizeToBeRead: int) -> None:
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

    # Parse the azBlobDirPath to extract the storage account name
    if azBlobDirPath.startswith("az://"):
        storage_account_name = azBlobDirPath.split("//")[1].split(".")[0]
    else:
        raise Exception("Invalid azBlobDirPath format. Expected format: az://<storage_account_name>.<rest_of_path>")

    # Check if STORAGE_INTCPT_SAS_TOKEN is set
    storage_sas_token = os.environ.get("STORAGE_INTCPT_SAS_TOKEN")
    if not storage_sas_token:
        raise Exception("STORAGE_INTCPT_SAS_TOKEN environment variable is not set. Please set it to run the test.")

    # Initialize the DACSCacheClient
    dacs_cache_client_instance = DACSCacheClient(storage_account_name, storage_sas_token)

    blob_dict = {}
    start = time.time()
    listings_counter = 0
    entries = 0

    # List all files in the test directory
    async def list_all_files_in_testdir() -> None:
        nonlocal listings_counter, entries, blob_dict
        async for entry in dacs_cache_client_instance.list_dir(azBlobDirPath):
            blob_name = entry.get("d_name")
            logger.debug(f"DACS_Cache_Client_Test:Found entry:{entry}")
            listings_counter += 1
            if blob_name != "." and blob_name != ".." and (not blobNamePattern or blobNamePattern in blob_name):
                blob_dict[entries] = {"name": blob_name, "size": entry.get("d_size")}
                entries += 1

    asyncio.run(list_all_files_in_testdir())
    logger.debug(
        f"DACS_Cache_Client_Test:Listed all {listings_counter} entries in {azBlobDirPath} in {time.time() - start:.2f} seconds"
    )
    blob_ids = list(blob_dict.keys())
    if len(blob_ids) == 0:
        logger.error("No blobs found matching the specified pattern. Exiting the test.")
        return
    logger.info(f"Found {len(blob_ids)} blobs with pattern:{blobNamePattern}")
    for i in blob_ids:
        logger.info(f"File name:{blob_dict[i]['name']}, size:{blob_dict[i]['size']} bytes")

    read_requests = 0
    total_bytes_read = 0
    start = time.time()

    # Read the blobs
    async def read_blobs(chunk_offset: int = 0, in_read_size: int = 0) -> None:
        nonlocal read_requests, total_bytes_read
        read_requests += 1
        logger.info(
            f"\n\n                       Read requests:{read_requests}, chunk_offset:{chunk_offset}, in_read_size:{in_read_size}\n\n"
        )
        for i in range(nBlobs):
            # Construct the full path to the blob
            blob_path = os.path.join(azBlobDirPath, blob_dict[i]["name"])

            # Use min of blob_dict[i]["size"] and in_read_size as the read size if set, otherwise use blob_dict[i]["size"]
            read_size = min(blob_dict[i]["size"], in_read_size) if in_read_size > 0 else blob_dict[i]["size"]

            # Allocate buffer for reading
            buffer = get_buffer_to_read(read_size)
            if not buffer:
                raise MemoryError("Failed to allocate buffer for reading blob")
            logger.info(
                f"Reading blob {blob_path} with size {blob_dict[i]['size']} bytes, chunk_offset:{chunk_offset}, read_size:{read_size}"
            )
            size_read = await dacs_cache_client_instance.read_blob(blob_path, buffer, read_size, chunk_offset)
            total_bytes_read += size_read

            # Once the read is complete, perform the required operation and then we should release the buffer
            release_buffer(buffer)

    if not blob_dict or len(blob_dict) == 0:
        logger.error("No blobs found to read. Ensure the directory contains valid blobs matching the criteria.")
        return

    asyncio.run(read_blobs(0, blobSizeToBeRead))

    # Uncomment the following lines to test different chunk sizes and offsets
    # asyncio.run(read_blobs(0, 32 * 1024 * 1024))
    # asyncio.run(read_blobs(1024 * 1024, 31 * 1024 * 1024))
    # asyncio.run(read_blobs(0, 31 * 1024 * 1024))
    # asyncio.run(read_blobs(1024 * 1024, 32 * 1024 * 1024))
    # asyncio.run(read_blobs(0, 32 * 1024 * 1024))
    # asyncio.run(read_blobs(0, 1 * 1024 * 1024))
    # asyncio.run(read_blobs(1024 * 1024, 31 * 1024 * 1024))
    # asyncio.run(read_blobs(0, 31 * 1024 * 1024))
    # asyncio.run(read_blobs(0, 32 * 1024 * 1024))
    # asyncio.run(read_blobs(0, 1 * 1024 * 1024))
    # asyncio.run(read_blobs(0, 64 * 1024 * 1024))
    # asyncio.run(read_blobs(0, 128 * 1024 * 1024))
    # asyncio.run(read_blobs(0, 5 * 1024 * 1024 * 1024))

    downloadSpeedMBps = (total_bytes_read / (time.time() - start)) / (1024 * 1024)
    logger.info(
        f"DACS_Cache_Client_Test:Read {read_requests} chunks with {total_bytes_read} bytes ({total_bytes_read /(1024*1024*1024)} GBs) in {time.time() - start:.2f} seconds at {downloadSpeedMBps / 128} Gbps"
    )

    # Shutdown the client
    asyncio.run(dacs_cache_client_instance.shutdown())


# main entrypoint
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Run DACS Cache Client performance test.")
    parser.add_argument("--nBlobs", type=int, default=1, help="Number of blobs to read")
    parser.add_argument(
        "--azBlobDirPath",
        type=str,
        default="az://storageinterceptionpr1.blob.core.windows.net/dataset/rojin_huge_file",
        help="Blob directory path to be tested",
    )
    parser.add_argument(
        "--blobNamePattern", type=str, default="1000G", help="Specific file name to be used for testing"
    )
    parser.add_argument("--blobSizeToBeRead", type=int, default=0, help="Size of the blob to be read")
    args = parser.parse_args()

    logger.info(
        f"DACS_Cache_Client_Test:Starting Perf test with nBlobs:{args.nBlobs} azBlobDirPath:{args.azBlobDirPath} blobNamePattern:{args.blobNamePattern}"
    )
    start_time = time.time()
    perf_test(args.nBlobs, args.azBlobDirPath, args.blobNamePattern, args.blobSizeToBeRead)
    logger.info(
        f"DACS_Cache_Client_Test:\n\n\t\tPerf test completed in {time.time() - start_time:.2f} seconds\n\n"
    )
