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

"""Hugging Face downloader access method — reads shards directly from HF Hub."""

import subprocess, sys

try:
    from huggingface_hub import HfApi, hf_hub_download
except ImportError:
    subprocess.check_call([sys.executable, "-m", "pip", "install", "-q", "huggingface_hub"],
                          stdout=sys.stderr, stderr=sys.stderr)
    from huggingface_hub import HfApi, hf_hub_download


def discover_shards(config):
    """List .safetensors files from a Hugging Face repo."""
    api = HfApi()
    files = api.list_repo_files(config["hf_repo"])
    return sorted([f for f in files if f.endswith(".safetensors")])


def read_shard(filename, config):
    """Download a single shard from HF Hub and read it into memory."""
    local_path = hf_hub_download(
        repo_id=config["hf_repo"],
        filename=filename,
        cache_dir="/tmp/hf_cache",
    )
    total_bytes = 0
    buf = bytearray(config["buffer_size"])
    with open(local_path, "rb") as f:
        while True:
            n = f.readinto(buf)
            if not n:
                break
            total_bytes += n
    return total_bytes
