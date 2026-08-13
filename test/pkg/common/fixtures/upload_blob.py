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

"""Upload deterministic test data to Azure Blob Storage.

Generates a repeatable byte pattern and uploads it to the specified blob.
Skips upload if the blob already exists with the expected size.

Environment variables:
    AZURE_STORAGE_ACCOUNT_NAME - Azure storage account name
    STORAGE_CONTAINER          - Azure storage container name
    BLOB_NAME                  - Name/path of the blob to create
    BLOB_SIZE                  - Size in bytes of the test data
    BLOB_PATTERN               - Repeating byte pattern (default: TEST_PAYLOAD)
"""

import os
import sys

import blobfile


def main():
    account = os.environ["AZURE_STORAGE_ACCOUNT_NAME"]
    container = os.environ.get("STORAGE_CONTAINER", "test-data")
    blob_name = os.environ["BLOB_NAME"]
    size = int(os.environ["BLOB_SIZE"])
    pattern = os.environ.get("BLOB_PATTERN", "TEST_PAYLOAD").encode()

    blob_url = f"az://{account}/{container}/{blob_name}"

    print(f"Provisioning {size} bytes to {blob_url}", flush=True)

    # Check if blob already exists with correct size
    try:
        stat = blobfile.stat(blob_url)
        if stat.size == size:
            print(f"Blob already exists with correct size ({size} bytes), skipping upload", flush=True)
            sys.exit(0)
    except FileNotFoundError:
        pass

    # Create deterministic test data and upload in chunks to avoid OOM
    chunk = pattern * (1024 // len(pattern) + 1)
    # Build a 1 MiB write buffer from the repeating pattern
    write_buf_size = 1024 * 1024
    write_buf = (chunk * (write_buf_size // len(chunk) + 1))[:write_buf_size]

    written = 0
    with blobfile.BlobFile(blob_url, "wb") as f:
        while written < size:
            remaining = size - written
            if remaining < write_buf_size:
                f.write(write_buf[:remaining])
                written += remaining
            else:
                f.write(write_buf)
                written += write_buf_size
    print(f"Uploaded {written} bytes", flush=True)


if __name__ == "__main__":
    main()
