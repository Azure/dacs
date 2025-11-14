# --------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# Sample code to integrate and test, APIs written based on DACS Cache Client
# --------------------------------------------------------------------------------------------------------------------
import argparse
import asyncio
import os
import sys
import time
from py_dacs_cache_client.dacs_cache_utils import logger
from py_dacs_cache_client.utils.dacs_cache_client_wrappers import download_remote_blobs_to_disk

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

    # Check if STORAGE_INTCPT_SAS_TOKEN is set
    storage_sas_token = os.environ.get("STORAGE_INTCPT_SAS_TOKEN")
    if not storage_sas_token:
        raise Exception("STORAGE_INTCPT_SAS_TOKEN environment variable is not set. Please set it to run the test.")

    # Check and create the local download path
    if not os.path.exists(localDownloadPath):
        os.makedirs(localDownloadPath)
    else:
        os.system(f"rm -rf {localDownloadPath}/*")

    download_remote_blobs_to_disk(storage_sas_token, azBlobDirPath, localDownloadPath)


# main entrypoint
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Run DACS Cache Client Download to Local Disk test.")
    parser.add_argument(
        "--azBlobDirPath",
        type=str,
        default="az://storageinterceptionpr1.blob.core.windows.net/dataset/rojin_huge_file/download_datasets",
        help="Blob directory path to be tested",
    )
    parser.add_argument(
        "--localDownloadPath",
        type=str,
        default="/tmp/dacs_cache_client_test",
        help="Local Disk path to download the blobs",
    )
    args = parser.parse_args()

    logger.info(
        f"DACS_Cache_Client_Test:Starting to download blobs from {args.azBlobDirPath} to {args.localDownloadPath}"
    )
    start_time = time.time()
    download_remote_blobs_to_disk_test(args.azBlobDirPath, args.localDownloadPath)
    logger.info(
        f"\n\n\t\tDACS_Cache_Client_Test:Download completed in {time.time() - start_time:.2f} seconds\n\n"
    )
