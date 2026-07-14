# --------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.
# Licensed under the Apache License, Version 2.0.
# Sample code to integrate and test, APIs written based on DACS Client
# --------------------------------------------------------------------------------------------------------------------
import argparse
import asyncio
import os
import shutil
import sys
import time
from py_dacs_client.dacs_utils import logger
from py_dacs_client.utils.dacs_client_wrappers import download_remote_blobs_to_disk


# Sample code for reference and also to be used in the test
def download_remote_blobs_to_disk_test(azBlobDirPath: str, localDownloadPath: str) -> None:
    """
    Perform a test by downloading blobs from a given Az Blob directory to a local path.
    Args:
        azBlobDirPath (str): Azure Blob directory path containing the blobs.
        localDownloadPath (str): Local directory path to download the blobs.
    """
    # Check if the azBlobDirPath is set
    if not azBlobDirPath:
        raise Exception("azBlobDirPath is not set. Please set it to run the test.")
    # Check if the azBlobDirPath is a valid path
    if not azBlobDirPath.startswith("az://"):
        raise Exception(
            "azBlobDirPath is not a valid path. Expected format: az://<storage_account_name>.<rest_of_path>"
        )

    # Check and create the local download path
    if not os.path.exists(localDownloadPath):
        os.makedirs(localDownloadPath)
    else:
        for filename in os.listdir(localDownloadPath):
            file_path = os.path.join(localDownloadPath, filename)
            if os.path.isfile(file_path) or os.path.islink(file_path):
                os.unlink(file_path)
            elif os.path.isdir(file_path):
                shutil.rmtree(file_path)
    download_remote_blobs_to_disk(azBlobDirPath, localDownloadPath)


# main entrypoint
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Run DACS Client Download to Local Disk test.")
    parser.add_argument(
        "--azBlobDirPath",
        type=str,
        default="az://storageinterceptionpr1.blob.core.windows.net/dataset/rojin_huge_file/download_datasets",
        help="Blob directory path to be tested",
    )
    parser.add_argument(
        "--localDownloadPath",
        type=str,
        default="/tmp/dacs_client_test",
        help="Local Disk path to download the blobs",
    )
    args = parser.parse_args()

    logger.info(
        f"DACS_Cache_Client_Test:Starting to download blobs from {args.azBlobDirPath} to {args.localDownloadPath}"
    )
    start_time = time.time()
    download_remote_blobs_to_disk_test(args.azBlobDirPath, args.localDownloadPath)
    logger.info(f"\n\n\t\tDACS_Cache_Client_Test:Download completed in {time.time() - start_time:.2f} seconds\n\n")
