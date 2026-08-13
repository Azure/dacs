#!/usr/bin/env python3
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

# ---------------------------------------------------------------
# DACS Client - Overwrite (Consistent Mode) Tests for boostedblob adapter
#
# These tests verify overwrite support via write_single(overwrite=True)
# when consistent_mode=True.
# Consistent mode is initialized via the conftest.py fixture in this directory.
# NOTE: Must run in a separate pytest invocation from efficient-mode tests
# because both the Python _ClientManager singleton and the C++ config
# singleton cache values on first load.
# ---------------------------------------------------------------

import os
import sys
import pytest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

import dacs_boostedblob_adapter as bbb
from conftest import BASE_BLOB_PATH

AZURE_BLOB_PATH = f"{BASE_BLOB_PATH}/boostedblob_overwrite"

# Initialize singleton with consistent_mode before any tests use it
bbb._ClientManager.get_instance(consistent_mode=True)


def run_async(coro):
    """Helper to run async functions in sync context."""
    import asyncio
    try:
        loop = asyncio.get_event_loop()
        if loop.is_running():
            import concurrent.futures
            with concurrent.futures.ThreadPoolExecutor() as pool:
                return pool.submit(asyncio.run, coro).result()
        return loop.run_until_complete(coro)
    except RuntimeError:
        return asyncio.run(coro)


class TestBoostedBlobOverwriteNewFile:
    """Test creating new files via write_single in consistent mode."""

    def test_create_new_file(self):
        """New file creation should work with overwrite=True in consistent mode."""
        blob_path = f"{AZURE_BLOB_PATH}/test_new_{id(self)}.txt"
        run_async(bbb.write.write_single(blob_path, b"new file data", overwrite=True))

        content = run_async(bbb.read.read_single(blob_path))
        assert content == b"new file data"

    def test_create_new_file_without_overwrite_flag(self):
        """New file creation should also work with overwrite=False (default)."""
        blob_path = f"{AZURE_BLOB_PATH}/test_new_default_{id(self)}.txt"
        run_async(bbb.write.write_single(blob_path, b"default mode"))

        content = run_async(bbb.read.read_single(blob_path))
        assert content == b"default mode"


class TestBoostedBlobOverwriteExistingFile:
    """Test overwriting existing files via write_single in consistent mode."""

    def test_overwrite_false_on_existing_file_fails(self):
        """overwrite=False on an existing file must fail with an error, even in consistent mode."""
        blob_path = f"{AZURE_BLOB_PATH}/test_excl_existing_{id(self)}.txt"

        # Create the file first
        run_async(bbb.write.write_single(blob_path, b"original data"))

        # overwrite=False on existing file should fail
        with pytest.raises(Exception):
            run_async(bbb.write.write_single(blob_path, b"should not overwrite", overwrite=False))

    def test_overwrite_existing_file(self):
        """Write V1, then overwrite with V2, read back V2."""
        blob_path = f"{AZURE_BLOB_PATH}/test_overwrite_{id(self)}.txt"

        # Write V1
        run_async(bbb.write.write_single(blob_path, b"version 1"))

        # Overwrite with V2
        run_async(bbb.write.write_single(blob_path, b"version 2 - different", overwrite=True))

        # Read back — should be V2
        content = run_async(bbb.read.read_single(blob_path))
        assert content == b"version 2 - different"

    def test_overwrite_with_smaller_data(self):
        """Overwrite large file with smaller data."""
        blob_path = f"{AZURE_BLOB_PATH}/test_overwrite_smaller_{id(self)}.txt"

        run_async(bbb.write.write_single(blob_path, b"A" * 10000))
        run_async(bbb.write.write_single(blob_path, b"tiny", overwrite=True))

        content = run_async(bbb.read.read_single(blob_path))
        assert content == b"tiny"

    def test_overwrite_with_larger_data(self):
        """Overwrite small file with larger data."""
        blob_path = f"{AZURE_BLOB_PATH}/test_overwrite_larger_{id(self)}.txt"

        run_async(bbb.write.write_single(blob_path, b"small"))
        run_async(bbb.write.write_single(blob_path, b"B" * 10000, overwrite=True))

        content = run_async(bbb.read.read_single(blob_path))
        assert content == b"B" * 10000

    def test_multiple_overwrites(self):
        """Overwrite same file multiple times — final read should be last version."""
        blob_path = f"{AZURE_BLOB_PATH}/test_multi_overwrite_{id(self)}.txt"

        for i in range(5):
            run_async(bbb.write.write_single(blob_path, f"version {i}".encode(), overwrite=True))

        content = run_async(bbb.read.read_single(blob_path))
        assert content == b"version 4"


class TestBoostedBlobOverwriteStat:
    """Test that stat reflects new file after overwrite."""

    def test_stat_reflects_new_size(self):
        """After overwrite, stat().size should reflect new data."""
        blob_path = f"{AZURE_BLOB_PATH}/test_stat_overwrite_{id(self)}.txt"

        run_async(bbb.write.write_single(blob_path, b"X" * 5000))
        stat1 = run_async(bbb.stat(blob_path))
        assert stat1.size == 5000

        run_async(bbb.write.write_single(blob_path, b"Y" * 100, overwrite=True))
        stat2 = run_async(bbb.stat(blob_path))
        assert stat2.size == 100
