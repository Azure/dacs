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

"""Data provisioning — ensures model data exists in blob storage, mirrored from HF.

Checks if .safetensors files already exist under the blob prefix. If not,
downloads from Hugging Face and uploads to blob storage.

Environment variables:
    AZURE_STORAGE_ACCOUNT_NAME - Azure storage account name
    STORAGE_CONTAINER          - Azure storage container name
    BLOB_PREFIX                - Blob prefix path
    HF_REPO                    - Hugging Face repo ID
"""

import glob
import os
import subprocess
import sys

import blobfile


def main():
    storage_account = os.environ.get("AZURE_STORAGE_ACCOUNT_NAME", "")
    container = os.environ.get("STORAGE_CONTAINER", "test-data")
    blob_prefix = os.environ.get("BLOB_PREFIX", "")
    hf_repo = os.environ.get("HF_REPO", "")

    print("Provisioning model data...", flush=True)
    print(f"  Account:   {storage_account}", flush=True)
    print(f"  Container: {container}", flush=True)
    print(f"  Prefix:    {blob_prefix}", flush=True)
    print(f"  HF Repo:   {hf_repo}", flush=True)

    base_url = f"az://{storage_account}/{container}/{blob_prefix}"

    # Check if data already exists
    try:
        existing = list(blobfile.listdir(base_url))
        safetensor_files = [f for f in existing if f.endswith(".safetensors")]
        if safetensor_files:
            total_size = 0
            print(f"Found {len(safetensor_files)} .safetensors files in blob:", flush=True)
            for f in sorted(safetensor_files):
                stat = blobfile.stat(f"{base_url}/{f}")
                total_size += stat.size
                print(f"  {f}: {stat.size / (1024**3):.2f} GB", flush=True)
            print(f"  Total: {total_size / (1024**3):.2f} GB", flush=True)
            print("Data provisioning: SKIPPED (already exists)", flush=True)
            return
    except Exception as e:
        print(f"No existing data found ({e}), will download from HF", flush=True)

    # Install huggingface_hub if not present
    try:
        import huggingface_hub
    except ImportError:
        print("Installing huggingface_hub...", flush=True)
        subprocess.check_call(
            [sys.executable, "-m", "pip", "install", "--quiet", "huggingface_hub"]
        )
        import huggingface_hub

    from huggingface_hub import snapshot_download

    print(f"\nDownloading model from Hugging Face: {hf_repo}", flush=True)
    try:
        local_dir = snapshot_download(
            repo_id=hf_repo,
            cache_dir="/tmp/hf_cache",
            allow_patterns=["*.safetensors", "*.json"],
            local_dir="/tmp/model",
        )
    except Exception as e:
        print(f"ERROR: Failed to download from HuggingFace: {e}", flush=True)
        sys.exit(1)

    safetensor_files = sorted(glob.glob(os.path.join(local_dir, "*.safetensors")))
    json_files = sorted(glob.glob(os.path.join(local_dir, "*.json")))
    all_files = safetensor_files + json_files

    print(f"Downloaded {len(safetensor_files)} .safetensors + {len(json_files)} .json files", flush=True)

    print(f"\nUploading to blob storage: {base_url}", flush=True)
    for filepath in all_files:
        filename = os.path.basename(filepath)
        dest_url = f"{base_url}/{filename}"
        file_size = os.path.getsize(filepath)
        print(f"  Uploading {filename} ({file_size/(1024**3):.2f} GB)...", flush=True)
        try:
            with open(filepath, "rb") as src:
                with blobfile.BlobFile(dest_url, "wb") as dst:
                    while True:
                        chunk = src.read(64 * 1024 * 1024)
                        if not chunk:
                            break
                        dst.write(chunk)
            print(f"    Done", flush=True)
        except Exception as e:
            print(f"  ERROR: Failed to upload {filename}: {e}", flush=True)
            sys.exit(1)

    print(f"\nData provisioning: COMPLETE ({len(safetensor_files)} shards uploaded)", flush=True)


if __name__ == "__main__":
    main()
