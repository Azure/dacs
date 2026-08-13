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

# Wrapper APIs build from function exposed by DACS Client
# --------------------------------------------------------------------------------------------------------------------
#
# This module provides wrapper APIs for the DACS Client.
# It includes functions to download blobs from Azure Blob Storage to local disk.
#
# Functions:
#    - download_remote_blobs_to_disk: Download all blobs from a given Az Blob directory to a local path.
#
# Notes:
#     - The module uses multiprocessing to download blobs in parallel.
# --------------------------------------------------------------------------------------------------------------------
import asyncio
import concurrent.futures
import multiprocessing
import os
import time
from ctypes import c_void_p
from py_dacs_client.dacs_utils import logger, get_cpu_cores, get_memory_limit
from py_dacs_client.dacs_utils import BoundedBufferManager
from py_dacs_client.dacs_client import DACSCacheClient
from typing import Dict, AsyncIterator


async def recursive_list_blobs(
    dacs_client_instance: DACSCacheClient, blob_dirname: str
) -> tuple[dict, list[str]]:
    """
    Recursively list blobs in the given directory and store them in the blob_dict.
    Args:
        dacs_client_instance (DACSCacheClient): Instance of the DACSCacheClient.
        blob_dirname (str): Directory name to list blobs from.
    Returns:
        tuple: A tuple containing:
            - file_dict (dict): Dictionary to store file paths. Key is file path and value is file size
            - dir_list (str[]): List to store directory paths.
    """

    logger.info(f"Listing blobs in directory: {blob_dirname}")
    file_dict = {}
    dir_list = []
    subdir_list_tasks = []

    async for entry in dacs_client_instance.list_dir(blob_dirname):
        if entry.get("d_name") == "." or entry.get("d_name") == "..":
            continue

        entry_path = os.path.join(blob_dirname, entry.get("d_name"))
        if entry.get("d_type") == 4:
            # Directory
            dir_list.append(entry_path)
            subdir_list_tasks.append(
                asyncio.create_task(recursive_list_blobs(dacs_client_instance, entry_path))
            )
        else:
            # File
            file_dict[entry_path] = entry.get("d_size")
            logger.debug(f"File: {entry_path}, Size: {entry.get('d_size')}")

    subdir_list_results = await asyncio.gather(*subdir_list_tasks)
    for subdir_file_dict, subdir_dir_list in subdir_list_results:
        file_dict.update(subdir_file_dict)
        dir_list.extend(subdir_dir_list)

    return file_dict, dir_list


async def fetch_blob_chunk(
    dacs_client_instance: DACSCacheClient,
    blob_name: str,
    offset: int,
    chunk_size: int,
    chunkIdx: int,
    read_buffer_manager: BoundedBufferManager,
) -> c_void_p:

    buffer = await read_buffer_manager.acquire_buffer(chunk_size)

    logger.info(f"Started reading chunkIdx={chunkIdx} of blob {blob_name} at offset {offset} with size {chunk_size}")
    await dacs_client_instance.read_blob(blob_name, buffer, chunk_size, offset)
    logger.info(f"Finished reading chunkIdx={chunkIdx} of blob {blob_name} at offset {offset} with size {chunk_size}")

    return buffer


async def read_iter_blob_chunks(
    dacs_client_instance: DACSCacheClient,
    blob_name: str,
    blob_size: int,
    chunk_size: int,
    read_buffer_manager: BoundedBufferManager,
) -> AsyncIterator[c_void_p]:
    """
    Read blob chunks from the given blob name.
    Args:
        dacs_client_instance (DACSCacheClient): Instance of the DACSCacheClient.
        blob_name (str): Blob name to read.
        blob_size (int): Size of the blob to be read.
        chunk_size (int): Size of each chunk to be read.
        executor (concurrent.futures.Executor): Executor for running blocking I/O operations.
    Yields:
        bytes: Blob chunks (as bytes) in order.
    """

    offset = 0
    chunkIdx = 0
    chunk_read_tasks = []
    while offset < blob_size:
        chunk_read_size = min(chunk_size, blob_size - offset)
        chunk_read_task = asyncio.create_task(
            fetch_blob_chunk(
                dacs_client_instance, blob_name, offset, chunk_read_size, chunkIdx, read_buffer_manager
            )
        )
        chunk_read_tasks.append(chunk_read_task)
        offset += chunk_size
        chunkIdx += 1

    chunkIdx = 0
    lastChunkIdx = len(chunk_read_tasks) - 1
    for chunk_read_task in chunk_read_tasks:
        chunk = await chunk_read_task

        logger.info(f"Yielding chunkIdx={chunkIdx}/{lastChunkIdx} with {len(chunk)} bytes for blob {blob_name}")
        chunkIdx += 1
        yield chunk


async def download_blob_to_local(
    dacs_client_instance: DACSCacheClient,
    blob_name: str,
    local_path: str,
    blob_size: int,
    chunk_size: int,
    threadpool: concurrent.futures.ThreadPoolExecutor,
    buffer_manager: BoundedBufferManager,
) -> None:
    """
    Download a blob file from the given blob name to the local path.
    Args:
        dacs_client_instance (DACSCacheClient): Instance of the DACSCacheClient.
        blob_name (str): Blob name to download.
        local_path (str): Local path to save the downloaded file.
        blob_size (int): Size of the blob to be downloaded.
        chunk_size (int): Size of each chunk to be downloaded and written.
        threadpool (concurrent.futures.ThreadPoolExecutor): Thread pool for running blocking I/O operations.
        buffer_manager (BoundedBufferManager): Buffer manager for managing blob chunk buffers.
    """

    loop = asyncio.get_event_loop()
    writeHandle = open(local_path, "wb")
    with writeHandle as f:
        chunkIdx = 0
        async for chunk in read_iter_blob_chunks(
            dacs_client_instance, blob_name, blob_size, chunk_size, buffer_manager
        ):
            chunk_len = len(chunk)
            # Write the chunk to the file
            logger.info(f"Writing chunkIdx={chunkIdx} with {chunk_len} bytes to {local_path}")
            write_exception = None
            try:
                await loop.run_in_executor(threadpool, f.write, memoryview(chunk))
            except Exception as e:
                write_exception = e
                logger.error(f"Error writing chunkIdx={chunkIdx} to {local_path}: {e}")
            finally:
                try:
                    await buffer_manager.release_buffer(chunk)
                except Exception as release_exc:
                    if write_exception is not None:
                        logger.error(
                            f"Failed to release chunkIdx={chunkIdx} after write error for {local_path}: {release_exc}"
                        )
                    else:
                        raise

            if write_exception is not None:
                raise write_exception

            logger.info(
                f"Wrote chunkIdx={chunkIdx} with {chunk_len} bytes to {local_path}, total size={blob_size} bytes"
            )

            chunkIdx += 1

    logger.info(f"Downloaded blob {blob_name} to {local_path} with size {blob_size} bytes")


async def download_remote_blob_to_disk_async(blob_dirname: str, local_dir: str) -> None:
    """
    Download a folder from the given blob directory to the local directory.
    Args:
        blob_dirname (str): Blob directory name to download.
        local_dir (str): Local directory to save the downloaded files.
    """

    logger.info(f"Starting download of {blob_dirname} to {local_dir}")

    default_threadpool_size = min(get_cpu_cores() * 4, 48)
    threadpool_size = int(os.environ.get("DACS_PY_CACHE_CLIENT_THREADPOOL_SIZE", default_threadpool_size))
    threadpool = concurrent.futures.ThreadPoolExecutor(max_workers=threadpool_size)
    logger.info(f"Using Threadpool size: {threadpool_size}")

    default_chunk_size = 256 * 1024 * 1024  # 256 MB
    chunk_size = int(os.environ.get("DACS_PY_CACHE_CLIENT_CHUNK_SIZE", default_chunk_size))

    buffer_limit = (threadpool_size + 5) * chunk_size  # enough read buffers to keep the threadpool busy + some extra
    available_memory = get_memory_limit()
    if buffer_limit > available_memory * 0.8:
        buffer_limit = int(available_memory * 0.8)
        logger.warning(f"Buffer limit exceeds available memory. Adjusting to {buffer_limit} bytes")

    buffer_limiter = BoundedBufferManager(max_memory_limit=buffer_limit)
    logger.info(f"Using read buffer limit: {buffer_limit} bytes")

    if not blob_dirname.startswith("az://"):
        raise ValueError(f"Invalid blob directory name: {blob_dirname}. It should start with 'az://'")
    dacs_client_instance = DACSCacheClient(threadpool)

    time_start = time.time()
    file_dict = {}
    dir_list = []
    file_dict, dir_list = await recursive_list_blobs(dacs_client_instance, blob_dirname)
    total_time_listdir = time.time() - time_start
    logger.info(f"Listed {len(file_dict)} files and {len(dir_list)} directories in {total_time_listdir:.2f} seconds")

    # Create the directories if it doesn't exist
    time_start_dir_creation = time.time()
    os.makedirs(local_dir, exist_ok=True)
    for dir_path in dir_list:
        local_dir_path = os.path.join(local_dir, os.path.relpath(dir_path, blob_dirname))
        os.makedirs(local_dir_path, exist_ok=True)
    total_time_dir_creation = time.time() - time_start_dir_creation
    logger.info(f"Created {len(dir_list)} directories in {total_time_dir_creation:.2f} seconds")

    # Download the files
    time_start_download = time.time()
    file_download_tasks = []
    for blob_name, blob_size in file_dict.items():
        local_path = os.path.join(local_dir, os.path.relpath(blob_name, blob_dirname))
        file_download_tasks.append(
            asyncio.create_task(
                download_blob_to_local(
                    dacs_client_instance,
                    blob_name,
                    local_path,
                    blob_size,
                    chunk_size,
                    threadpool,
                    buffer_limiter,
                )
            )
        )
    await asyncio.gather(*file_download_tasks)
    total_time_download = time.time() - time_start_download
    logger.info(f"Downloaded {len(file_dict)} files in {total_time_download:.2f} seconds")

    threadpool.shutdown(wait=True)
    await dacs_client_instance.shutdown()
    total_time = time.time() - time_start

    total_blob_size_bytes = sum(file_dict.values())
    total_blob_size_megabits = (8 * total_blob_size_bytes) / (1024 * 1024)
    download_speed_mbps = (total_blob_size_megabits) / total_time_download
    download_speed_gbps = download_speed_mbps / 1024
    logger.info(
        f"Download completed for folder {blob_dirname} to {local_dir}.\
        \n Download statistics: \
        \n\tTotal size: {total_blob_size_bytes} bytes \
        \n\tTotal time: {total_time:.2f} seconds \
        \n\tDownload speed: {download_speed_mbps:.2f} Mbps ({download_speed_gbps:.2f} Gbps) \
        \n\tTotal time for listing: {total_time_listdir:.2f} seconds \
        \n\tTotal time for directory creation: {total_time_dir_creation:.2f} seconds \
        \n\tTotal time for download: {total_time_download:.2f} seconds"
    )


def download_remote_blobs_to_disk(remote_blob_uri: str, local_disk_path: str) -> None:
    """
    Download all blobs from a given Az Blob directory to a local path.
    Args:
        remote_blob_uri (str): Azure Blob directory path containing the blobs.
        local_disk_path (str): Local directory path to download the blobs.
    """

    asyncio.run(download_remote_blob_to_disk_async(remote_blob_uri, local_disk_path))
