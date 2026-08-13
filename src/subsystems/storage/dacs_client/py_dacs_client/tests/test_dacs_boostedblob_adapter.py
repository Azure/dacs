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
# DACS Client - Integration Tests for dacs_boostedblob_adapter API
# ---------------------------------------------------------------
# Tests the boostedblob-compatible async API provided by dacs_boostedblob_adapter.
# Validates API parity with native boostedblob for supported operations.
# Test structure aligned with test_dacs_blobfile_adapter.py for consistency.
# ---------------------------------------------------------------

import asyncio
import inspect
import os
import sys
import time
import pytest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import dacs_boostedblob_adapter as bbb
from dacs_boostedblob_adapter import (
    AzurePath,
    BasePath,
    Stat,
    DirEntry,
    OperationNotSupportedError,
    ByteRange,
    OptByteRange,
    BoostExecutor,
    ensure_session,
)

from conftest import BASE_BLOB_PATH, TEST_SESSION_ID

# Use adapter-specific sub-path to avoid collisions with blobfile adapter tests
AZURE_BLOB_PATH = f"{BASE_BLOB_PATH}/boostedblob"


def run_async(coro):
    """Helper to run async functions in sync context."""
    try:
        loop = asyncio.get_running_loop()
    except RuntimeError:
        loop = None
    if loop and loop.is_running():
        raise RuntimeError("Cannot use run_async inside a running event loop")
    return asyncio.run(coro)


# ---------------------------------------------------------------
# Path Conversion Tests
# ---------------------------------------------------------------

class TestAzurePath:
    """Test AzurePath parsing and operations."""

    def test_from_str_short_form(self):
        """Test parsing short form az:// path."""
        path = AzurePath.from_str("az://myaccount/mycontainer/myblob.txt")
        assert path.account == "myaccount"
        assert path.container == "mycontainer"
        assert path.blob == "myblob.txt"

    def test_from_str_long_form(self):
        """Test parsing long form az:// path."""
        path = AzurePath.from_str("az://myaccount.blob.core.windows.net/mycontainer/myblob.txt")
        assert path.account == "myaccount"
        assert path.container == "mycontainer"
        assert path.blob == "myblob.txt"

    def test_from_str_https_form(self):
        """Test parsing https:// path."""
        path = AzurePath.from_str("https://myaccount.blob.core.windows.net/mycontainer/myblob.txt")
        assert path.account == "myaccount"
        assert path.container == "mycontainer"
        assert path.blob == "myblob.txt"

    def test_from_str_container_only(self):
        """Test parsing path with container but no blob."""
        path = AzurePath.from_str("az://myaccount/mycontainer")
        assert path.account == "myaccount"
        assert path.container == "mycontainer"
        assert path.blob == ""

    def test_from_str_nested_blob(self):
        """Test parsing path with nested blob path."""
        path = AzurePath.from_str("az://myaccount/container/dir1/dir2/file.txt")
        assert path.account == "myaccount"
        assert path.container == "container"
        assert path.blob == "dir1/dir2/file.txt"

    def test_invalid_scheme(self):
        """Test that invalid schemes raise ValueError."""
        with pytest.raises(ValueError, match="expected 'https' or 'az' scheme"):
            AzurePath.from_str("ftp://myaccount/container/blob")

    def test_invalid_https_host(self):
        """Test that invalid https host raises ValueError."""
        with pytest.raises(ValueError, match="unexpected host"):
            AzurePath.from_str("https://myaccount.dfs.core.windows.net/container/blob")

    def test_path_operations(self):
        """Test AzurePath name, parent, and join operations."""
        path = AzurePath.from_str("az://account/container/dir/file.txt")
        assert path.name == "file.txt"
        
        parent = path.parent
        assert parent.blob == "dir"
        
        joined = parent / "newfile.txt"
        assert joined.blob == "dir/newfile.txt"


# ---------------------------------------------------------------
# Read Operations Tests
# ---------------------------------------------------------------

class TestReadOperations:
    """Test read operations."""

    def test_read_single_roundtrip(self):
        """Test write and read single blob."""
        test_data = b"Hello, DACS BoostedBlob!"
        blob_path = f"{AZURE_BLOB_PATH}/test_read_single_{id(self)}.txt"
        
        run_async(bbb.write.write_single(blob_path, test_data))
        result = run_async(bbb.read.read_single(blob_path))
        assert result == test_data

    def test_read_single_not_found(self):
        """Test that reading non-existent blob raises error."""
        blob_path = f"{AZURE_BLOB_PATH}/nonexistent_{id(self)}.txt"
        with pytest.raises(Exception):
            run_async(bbb.read.read_single(blob_path))

    def test_read_byte_range(self):
        """Test reading byte ranges."""
        test_data = b"0123456789ABCDEF"
        blob_path = f"{AZURE_BLOB_PATH}/test_byte_range_{id(self)}.txt"
        run_async(bbb.write.write_single(blob_path, test_data))
        
        # Read middle portion
        result = run_async(bbb.read.read_byte_range(blob_path, (5, 10)))
        assert result == b"56789"
        
        # Read from start
        result = run_async(bbb.read.read_byte_range(blob_path, (0, 5)))
        assert result == b"01234"

    def test_read_byte_range_to_end(self):
        """Test reading byte range to end of file."""
        test_data = b"0123456789ABCDEF"
        blob_path = f"{AZURE_BLOB_PATH}/test_byte_range_end_{id(self)}.txt"
        run_async(bbb.write.write_single(blob_path, test_data))
        
        # Read from offset to end
        result = run_async(bbb.read.read_byte_range(blob_path, (10, len(test_data))))
        assert result == b"ABCDEF"

    def test_read_large_blob(self):
        """Test reading larger blob (100KB)."""
        test_data = b"X" * (100 * 1024)
        blob_path = f"{AZURE_BLOB_PATH}/test_read_large_{id(self)}.bin"
        
        run_async(bbb.write.write_single(blob_path, test_data))
        result = run_async(bbb.read.read_single(blob_path))
        assert len(result) == len(test_data)
        assert result == test_data


# ---------------------------------------------------------------
# Streaming Read Operations Tests
# ---------------------------------------------------------------

class TestStreamingReadOperations:
    """Test streaming read operations (read_stream, read_stream_unordered, read_chunked)."""

    def test_read_stream_basic(self):
        """Test read_stream yields chunks."""
        test_data = b"A" * (100 * 1024)  # 100KB
        blob_path = f"{AZURE_BLOB_PATH}/test_read_stream_{id(self)}.bin"
        run_async(bbb.write.write_single(blob_path, test_data))
        
        async def collect_chunks():
            chunks = []
            stream = await bbb.read.read_stream(blob_path)
            async for chunk in stream:
                chunks.append(chunk)
            return b"".join(chunks)
        
        result = run_async(collect_chunks())
        assert result == test_data

    def test_read_stream_with_size_hint(self):
        """Test read_stream with size hint (saves network call)."""
        test_data = b"B" * (50 * 1024)  # 50KB
        blob_path = f"{AZURE_BLOB_PATH}/test_read_stream_size_{id(self)}.bin"
        run_async(bbb.write.write_single(blob_path, test_data))
        
        async def collect_chunks():
            chunks = []
            stream = await bbb.read.read_stream(blob_path, size=len(test_data))
            async for chunk in stream:
                chunks.append(chunk)
            return b"".join(chunks)
        
        result = run_async(collect_chunks())
        assert result == test_data

    def test_read_stream_unordered_basic(self):
        """Test read_stream_unordered yields (chunk, byte_range) tuples."""
        test_data = b"C" * (100 * 1024)  # 100KB
        blob_path = f"{AZURE_BLOB_PATH}/test_read_stream_unord_{id(self)}.bin"
        run_async(bbb.write.write_single(blob_path, test_data))
        
        async def collect_chunks():
            chunks_with_ranges = []
            stream = await bbb.read.read_stream_unordered(blob_path)
            async for chunk, byte_range in stream:
                chunks_with_ranges.append((chunk, byte_range))
            return chunks_with_ranges
        
        results = run_async(collect_chunks())
        assert len(results) > 0
        
        # Verify each result is (bytes, (start, end)) tuple
        for chunk, byte_range in results:
            assert isinstance(chunk, bytes)
            assert isinstance(byte_range, tuple)
            assert len(byte_range) == 2
            start, end = byte_range
            assert isinstance(start, int)
            assert isinstance(end, int)
            assert start < end

    def test_read_chunked_basic(self):
        """Test read_chunked returns complete file content (via chunked reads)."""
        test_data = b"D" * (100 * 1024)  # 100KB
        blob_path = f"{AZURE_BLOB_PATH}/test_read_chunked_{id(self)}.bin"
        run_async(bbb.write.write_single(blob_path, test_data))
        
        result = run_async(bbb.read.read_chunked(blob_path))
        assert result == test_data

    def test_read_chunked_with_size(self):
        """Test read_chunked with size hint."""
        test_data = b"E" * (75 * 1024)  # 75KB
        blob_path = f"{AZURE_BLOB_PATH}/test_read_chunked_size_{id(self)}.bin"
        run_async(bbb.write.write_single(blob_path, test_data))
        
        result = run_async(bbb.read.read_chunked(blob_path, size=len(test_data)))
        assert result == test_data

    def test_read_stream_empty_file(self):
        """Test read_stream on empty file yields no chunks."""
        blob_path = f"{AZURE_BLOB_PATH}/test_read_stream_empty_{id(self)}.bin"
        run_async(bbb.write.write_single(blob_path, b""))
        
        async def collect_chunks():
            chunks = []
            stream = await bbb.read.read_stream(blob_path)
            async for chunk in stream:
                chunks.append(chunk)
            return chunks
        
        chunks = run_async(collect_chunks())
        assert chunks == []

    def test_read_chunked_empty_file(self):
        """Test read_chunked on empty file returns empty bytes."""
        blob_path = f"{AZURE_BLOB_PATH}/test_read_chunked_empty_{id(self)}.bin"
        run_async(bbb.write.write_single(blob_path, b""))
        
        result = run_async(bbb.read.read_chunked(blob_path))
        assert result == b""


# ---------------------------------------------------------------
# Byte Range Helper Tests
# ---------------------------------------------------------------

class TestByteRangeHelper:
    """Test byte_range_to_str helper function."""

    def test_byte_range_full_range(self):
        """Test full range (start, end) - HTTP range is inclusive on end."""
        result = bbb.byte_range_to_str((0, 100))
        assert result == "bytes=0-99"

    def test_byte_range_from_offset(self):
        """Test range from offset to end (start, None)."""
        result = bbb.byte_range_to_str((50, None))
        assert result == "bytes=50-"

    def test_byte_range_first_n_bytes(self):
        """Test first N bytes (None, end)."""
        result = bbb.byte_range_to_str((None, 100))
        assert result == "bytes=0-99"

    def test_byte_range_none_none(self):
        """Test full file (None, None) returns None."""
        result = bbb.byte_range_to_str((None, None))
        assert result is None


# ---------------------------------------------------------------
# Write Operations Tests
# ---------------------------------------------------------------

class TestWriteOperations:
    """Test write operations."""
    def test_write_single_types(self):
        """Test writing different data types."""
        blob_path = f"{AZURE_BLOB_PATH}/test_write_types_{id(self)}.txt"
        
        # Write bytes
        run_async(bbb.write.write_single(blob_path + "_bytes", b"bytes data"))
        assert run_async(bbb.read.read_single(blob_path + "_bytes")) == b"bytes data"
        
        # Write string
        run_async(bbb.write.write_single(blob_path + "_str", "string data"))
        assert run_async(bbb.read.read_single(blob_path + "_str")) == b"string data"

    def test_write_existing_file_fails(self):
        """Test that writing to existing file without overwrite fails."""
        blob_path = f"{AZURE_BLOB_PATH}/test_write_existing_{id(self)}.txt"
        run_async(bbb.write.write_single(blob_path, b"first"))
        
        with pytest.raises(Exception):
            run_async(bbb.write.write_single(blob_path, b"second", overwrite=False))

    def test_write_overwrite_not_supported_in_efficient_mode(self):
        """Test that overwrite=True raises OperationNotSupportedError in efficient mode."""
        blob_path = f"{AZURE_BLOB_PATH}/test_write_overwrite_{id(self)}.txt"
        run_async(bbb.write.write_single(blob_path, b"first content"))
        
        # Overwrite is not supported in efficient mode (default)
        with pytest.raises(OperationNotSupportedError):
            run_async(bbb.write.write_single(blob_path, b"second content", overwrite=True))

    def test_write_multiple_files(self):
        """Test writing multiple small files."""
        base_path = f"{AZURE_BLOB_PATH}/test_write_multi_{id(self)}"
        num_files = 5
        
        for i in range(num_files):
            run_async(bbb.write.write_single(f"{base_path}/file_{i}.txt", f"Content {i}".encode()))
        
        # Verify all files were written
        for i in range(num_files):
            result = run_async(bbb.read.read_single(f"{base_path}/file_{i}.txt"))
            assert result == f"Content {i}".encode()

    def test_write_empty_file(self):
        """Test writing empty file."""
        blob_path = f"{AZURE_BLOB_PATH}/test_write_empty_{id(self)}.txt"
        run_async(bbb.write.write_single(blob_path, b""))
        
        result = run_async(bbb.read.read_single(blob_path))
        assert result == b""

    def test_write_large_file(self):
        """Test writing larger file (100KB)."""
        test_data = b"X" * (100 * 1024)
        blob_path = f"{AZURE_BLOB_PATH}/test_write_large_{id(self)}.bin"
        
        run_async(bbb.write.write_single(blob_path, test_data))
        result = run_async(bbb.read.read_single(blob_path))
        assert len(result) == len(test_data)


# ---------------------------------------------------------------
# Exists Operations Tests
# ---------------------------------------------------------------

class TestExistsOperations:
    """Test exists operations."""
    def test_exists_after_write(self):
        """Test exists returns True after writing."""
        blob_path = f"{AZURE_BLOB_PATH}/test_exists_after_{id(self)}.txt"
        
        run_async(bbb.write.write_single(blob_path, b"data"))
        assert run_async(bbb.exists(blob_path)) is True

    def test_exists_nonexistent(self):
        """Test exists returns False for non-existent blob."""
        blob_path = f"{AZURE_BLOB_PATH}/nonexistent_exists_{id(self)}.txt"
        assert run_async(bbb.exists(blob_path)) is False

    def test_isfile(self):
        """Test isfile returns True for existing file."""
        blob_path = f"{AZURE_BLOB_PATH}/test_isfile_{id(self)}.txt"
        run_async(bbb.write.write_single(blob_path, b"data"))
        
        assert run_async(bbb.isfile(blob_path)) is True

    def test_isfile_nonexistent(self):
        """Test isfile returns False or raises error for non-existent blob."""
        blob_path = f"{AZURE_BLOB_PATH}/nonexistent_isfile_{id(self)}.txt"
        try:
            result = run_async(bbb.isfile(blob_path))
            assert result is False
        except Exception:
            # Some implementations raise an error for non-existent files
            pass


# ---------------------------------------------------------------
# Stat Operations Tests
# ---------------------------------------------------------------

class TestStatOperations:
    """Test stat operations."""
    def test_stat_basic(self):
        """Test stat returns Stat object with expected fields."""
        blob_path = f"{AZURE_BLOB_PATH}/test_stat_{id(self)}.txt"
        test_data = b"test content"
        run_async(bbb.write.write_single(blob_path, test_data))
        
        stat = run_async(bbb.stat(blob_path))
        assert isinstance(stat, Stat)
        assert stat.size == len(test_data)
        assert hasattr(stat, 'mtime')

    def test_stat_all_fields(self):
        """Test that stat() returns all expected fields."""
        blob_path = f"{AZURE_BLOB_PATH}/test_stat_fields_{id(self)}.txt"
        test_data = b"Complete stat field test"
        run_async(bbb.write.write_single(blob_path, test_data))
        
        stat = run_async(bbb.stat(blob_path))
        assert hasattr(stat, 'size')
        assert hasattr(stat, 'mtime')
        assert stat.size == len(test_data)

    def test_stat_nonexistent(self):
        """Test stat raises error for non-existent blob."""
        blob_path = f"{AZURE_BLOB_PATH}/nonexistent_stat_{id(self)}.txt"
        with pytest.raises(Exception):
            run_async(bbb.stat(blob_path))

    def test_stat_empty_blob(self):
        """Test stat on empty blob."""
        blob_path = f"{AZURE_BLOB_PATH}/test_stat_empty_{id(self)}.txt"
        run_async(bbb.write.write_single(blob_path, b""))
        
        stat = run_async(bbb.stat(blob_path))
        assert stat.size == 0

    def test_stat_large_blob(self):
        """Test stat on larger blob."""
        test_data = b"X" * 10000
        blob_path = f"{AZURE_BLOB_PATH}/test_stat_large_{id(self)}.txt"
        run_async(bbb.write.write_single(blob_path, test_data))
        
        stat = run_async(bbb.stat(blob_path))
        assert stat.size == 10000

    def test_getsize(self):
        """Test getsize returns correct size."""
        blob_path = f"{AZURE_BLOB_PATH}/test_getsize_{id(self)}.txt"
        test_data = b"x" * 1234
        run_async(bbb.write.write_single(blob_path, test_data))
        
        size = run_async(bbb.getsize(blob_path))
        assert size == 1234


# ---------------------------------------------------------------
# Directory Operations Tests
# ---------------------------------------------------------------

class TestDirectoryOperations:
    """Test directory listing operations."""

    def test_listdir_empty_directory(self):
        """Test listdir on empty/non-existent directory."""
        dir_path = f"{AZURE_BLOB_PATH}/test_listdir_empty_{id(self)}"
        
        async def collect_names():
            names = []
            try:
                async for path in bbb.listdir(dir_path):
                    if hasattr(path, 'name'):
                        names.append(path.name)
                    else:
                        names.append(os.path.basename(str(path)))
            except (FileNotFoundError, OSError):
                pass  # Empty/non-existent directory may raise error
            return names
        
        names = run_async(collect_names())
        assert isinstance(names, list)

    def test_listdir_with_files(self):
        """Test listdir returns files in directory."""
        dir_path = f"{AZURE_BLOB_PATH}/test_listdir_files_{id(self)}"
        file_names = ["file1.txt", "file2.txt", "file3.txt"]
        
        # Create files
        for name in file_names:
            run_async(bbb.write.write_single(f"{dir_path}/{name}", b"content"))
        
        # Wait for writes to propagate
        time.sleep(1)
        
        async def collect_names():
            names = []
            async for path in bbb.listdir(dir_path):
                if hasattr(path, 'name'):
                    names.append(path.name)
                else:
                    names.append(os.path.basename(str(path)))
            return names
        
        # Retry for eventual consistency
        for attempt in range(3):
            names = run_async(collect_names())
            if set(names) == set(file_names):
                break
            time.sleep(1)
        
        assert set(names) == set(file_names)

    def test_scandir_with_files(self):
        """Test scandir yields DirEntry objects."""
        dir_path = f"{AZURE_BLOB_PATH}/test_scandir_files_{id(self)}"
        file_names = ["scan1.txt", "scan2.txt", "scan3.txt"]
        
        for name in file_names:
            run_async(bbb.write.write_single(f"{dir_path}/{name}", b"content"))
        
        time.sleep(1)
        
        async def collect_entries():
            entries = []
            async for entry in bbb.scandir(dir_path):
                entries.append(entry)
            return entries
        
        # Retry for eventual consistency
        for attempt in range(3):
            entries = run_async(collect_entries())
            if len(entries) >= len(file_names):
                break
            time.sleep(1)
        
        assert len(entries) >= len(file_names)
        assert all(isinstance(e, DirEntry) for e in entries)

    def test_listdir_scandir_consistency(self):
        """Test that listdir and scandir return consistent results."""
        dir_path = f"{AZURE_BLOB_PATH}/test_listdir_scandir_{id(self)}"
        file_names = ["consistent1.txt", "consistent2.txt"]
        
        for name in file_names:
            run_async(bbb.write.write_single(f"{dir_path}/{name}", b"content"))
        
        time.sleep(1)
        
        async def get_listdir_names():
            names = []
            async for path in bbb.listdir(dir_path):
                if hasattr(path, 'name'):
                    names.append(path.name)
                else:
                    names.append(os.path.basename(str(path)))
            return set(names)
        
        async def get_scandir_names():
            names = []
            async for entry in bbb.scandir(dir_path):
                # DirEntry has .path attribute which is AzurePath with .name
                if hasattr(entry, 'path') and hasattr(entry.path, 'name'):
                    names.append(entry.path.name)
                elif hasattr(entry, 'name'):
                    names.append(entry.name)
                else:
                    names.append(str(entry))
            return set(names)
        
        listdir_items = run_async(get_listdir_names())
        scandir_items = run_async(get_scandir_names())
        
        assert listdir_items == scandir_items

    def test_listdir_generator_behavior(self):
        """Test that listdir returns an async iterator."""
        dir_path = f"{AZURE_BLOB_PATH}/test_listdir_gen_{id(self)}"
        run_async(bbb.write.write_single(f"{dir_path}/test.txt", b"test"))
        
        result = bbb.listdir(dir_path)
        assert hasattr(result, '__aiter__'), "listdir should return an async iterable"

    def test_scandir_generator_behavior(self):
        """Test that scandir returns an async iterator."""
        dir_path = f"{AZURE_BLOB_PATH}/test_scandir_gen_{id(self)}"
        run_async(bbb.write.write_single(f"{dir_path}/test.txt", b"test"))
        
        result = bbb.scandir(dir_path)
        assert hasattr(result, '__aiter__'), "scandir should return an async iterable"

    def test_isdir(self):
        """Test isdir for directory with files."""
        dir_path = f"{AZURE_BLOB_PATH}/test_isdir_{id(self)}"
        run_async(bbb.write.write_single(f"{dir_path}/file.txt", b"content"))
        
        time.sleep(1)
        
        is_dir = run_async(bbb.isdir(dir_path))
        assert is_dir is True

    def test_isdir_nonexistent(self):
        """Test isdir returns False for non-existent directory."""
        dir_path = f"{AZURE_BLOB_PATH}/nonexistent_dir_{id(self)}"
        is_dir = run_async(bbb.isdir(dir_path))
        assert is_dir is False


# ---------------------------------------------------------------
# Write Modes Tests
# ---------------------------------------------------------------

class TestWriteModes:
    """Test WriteMode functionality."""

    def test_write_mode_enum_values(self):
        """Test WriteMode enum has expected values."""
        assert hasattr(bbb.WriteMode, 'WRITE_DISABLED')
        assert hasattr(bbb.WriteMode, 'WRITE_SYNC')

    def test_default_write_mode(self):
        """Test that writes work with default mode."""
        blob_path = f"{AZURE_BLOB_PATH}/test_write_mode_{id(self)}.txt"
        run_async(bbb.write.write_single(blob_path, b"test data"))
        
        result = run_async(bbb.read.read_single(blob_path))
        assert result == b"test data"


# ---------------------------------------------------------------
# Unsupported Operations Tests
# ---------------------------------------------------------------

class TestUnsupportedOperations:
    """Test that unsupported operations raise OperationNotSupportedError."""

    def test_write_stream_not_supported(self):
        """Test write_stream raises OperationNotSupportedError."""
        with pytest.raises(OperationNotSupportedError):
            run_async(bbb.write_stream("path"))

    def test_write_stream_unordered_not_supported(self):
        """Test write_stream_unordered raises OperationNotSupportedError."""
        with pytest.raises(OperationNotSupportedError):
            run_async(bbb.write_stream_unordered("path"))

    def test_write_chunked_not_supported(self):
        """Test write_chunked raises OperationNotSupportedError."""
        with pytest.raises(OperationNotSupportedError):
            run_async(bbb.write_chunked("path", b"data"))

    def test_remove_not_supported(self):
        """Test remove raises OperationNotSupportedError."""
        with pytest.raises(OperationNotSupportedError):
            run_async(bbb.remove("path"))

    def test_rmtree_not_supported(self):
        """Test rmtree raises OperationNotSupportedError."""
        with pytest.raises(OperationNotSupportedError):
            run_async(bbb.rmtree("path"))

    def test_copyfile_not_supported(self):
        """Test copyfile raises OperationNotSupportedError."""
        with pytest.raises(OperationNotSupportedError):
            run_async(bbb.copyfile("src", "dst"))

    def test_copytree_not_supported(self):
        """Test copytree raises OperationNotSupportedError."""
        with pytest.raises(OperationNotSupportedError):
            run_async(bbb.copytree("src", "dst"))

    def test_sync_not_supported(self):
        """Test sync raises OperationNotSupportedError."""
        async def try_sync():
            async for _ in bbb.sync("src", "dst"):
                pass
        with pytest.raises(OperationNotSupportedError):
            run_async(try_sync())

    def test_get_url_not_supported(self):
        """Test get_url raises OperationNotSupportedError."""
        with pytest.raises(OperationNotSupportedError):
            run_async(bbb.share.get_url("path"))

    def test_list_blobs_not_supported(self):
        """Test list_blobs raises OperationNotSupportedError."""
        with pytest.raises(OperationNotSupportedError):
            bbb.list_blobs("path")

    def test_glob_scandir_not_supported(self):
        """Test glob_scandir raises OperationNotSupportedError."""
        with pytest.raises(OperationNotSupportedError):
            bbb.glob_scandir("path/*")


# ---------------------------------------------------------------
# Synthetic Submodules Tests
# ---------------------------------------------------------------

class TestSyntheticSubmodules:
    """Test synthetic submodules for boostedblob API parity."""

    def test_read_submodule_exists(self):
        """Test that bbb.read submodule exists."""
        assert hasattr(bbb, 'read')
        assert bbb.read is not None

    def test_read_submodule_functions(self):
        """Test that bbb.read has expected functions."""
        assert hasattr(bbb.read, 'read_single')
        assert hasattr(bbb.read, 'read_byte_range')
        assert hasattr(bbb.read, 'read_stream')
        assert hasattr(bbb.read, 'read_stream_unordered')
        assert hasattr(bbb.read, 'read_chunked')
        assert hasattr(bbb.read, 'byte_range_to_str')

    def test_read_submodule_types(self):
        """Test that bbb.read has type aliases."""
        assert hasattr(bbb.read, 'ByteRange')
        assert hasattr(bbb.read, 'OptByteRange')

    def test_write_submodule_exists(self):
        """Test that bbb.write submodule exists."""
        assert hasattr(bbb, 'write')
        assert bbb.write is not None

    def test_write_submodule_functions(self):
        """Test that bbb.write has expected functions."""
        assert hasattr(bbb.write, 'write_single')
        assert hasattr(bbb.write, 'write_stream')
        assert hasattr(bbb.write, 'write_stream_unordered')
        assert hasattr(bbb.write, 'write_chunked')

    def test_share_submodule_exists(self):
        """Test that bbb.share submodule exists."""
        assert hasattr(bbb, 'share')
        assert bbb.share is not None

    def test_share_submodule_functions(self):
        """Test that bbb.share has expected functions."""
        assert hasattr(bbb.share, 'get_url')

    def test_submodule_access_pattern(self):
        """Test that submodule functions are accessible via submodule pattern."""
        # Functions must be accessed via submodules (matching native boostedblob)
        assert callable(bbb.read.read_single)
        assert callable(bbb.write.write_single)
        assert callable(bbb.share.get_url)

    def test_read_via_submodule(self):
        """Test reading via bbb.read.read_single."""
        test_data = b"submodule read test"
        blob_path = f"{AZURE_BLOB_PATH}/test_submodule_read_{id(self)}.txt"
        
        run_async(bbb.write.write_single(blob_path, test_data))
        result = run_async(bbb.read.read_single(blob_path))
        assert result == test_data

    def test_write_via_submodule(self):
        """Test writing via bbb.write.write_single."""
        test_data = b"submodule write test"
        blob_path = f"{AZURE_BLOB_PATH}/test_submodule_write_{id(self)}.txt"
        
        run_async(bbb.write.write_single(blob_path, test_data))
        result = run_async(bbb.read.read_single(blob_path))
        assert result == test_data


# ---------------------------------------------------------------
# BoostExecutor and ensure_session Tests
# ---------------------------------------------------------------

class TestBoostExecutorAndSession:
    """Test BoostExecutor placeholder and ensure_session decorator."""

    def test_boost_executor_exists(self):
        """Test that BoostExecutor class exists."""
        assert hasattr(bbb, 'BoostExecutor')
        assert bbb.BoostExecutor is not None

    def test_boost_executor_instantiation(self):
        """Test that BoostExecutor can be instantiated."""
        executor = BoostExecutor(concurrency=8)
        assert executor.concurrency == 8

    def test_boost_executor_context_manager(self):
        """Test that BoostExecutor works as async context manager."""
        async def test():
            async with BoostExecutor(4) as executor:
                assert executor.concurrency == 4
        run_async(test())

    def test_ensure_session_exists(self):
        """Test that ensure_session decorator exists."""
        assert hasattr(bbb, 'ensure_session')
        assert bbb.ensure_session is not None

    def test_ensure_session_is_noop(self):
        """Test that ensure_session is a passthrough decorator."""
        @ensure_session
        async def my_func():
            return "test"
        
        # ensure_session should return the same function
        result = run_async(my_func())
        assert result == "test"


# ---------------------------------------------------------------
# API Parity Tests
# ---------------------------------------------------------------

class TestAPIParity:
    """Test API parity with native boostedblob."""

    def test_async_functions(self):
        """Test that core functions are async."""
        async_funcs = [
            bbb.read.read_single, bbb.read.read_byte_range, bbb.write.write_single,
            bbb.exists, bbb.stat, bbb.getsize, bbb.isdir, bbb.isfile,
            bbb.shutdown
        ]
        for func in async_funcs:
            assert asyncio.iscoroutinefunction(func), f"{func.__name__} should be async"

    def test_iterator_functions(self):
        """Test that listing functions return async iterators."""
        for func in [bbb.listdir, bbb.scandir, bbb.listtree, bbb.scantree]:
            assert callable(func), f"{func.__name__} should be callable"

    def test_types_exported(self):
        """Test that required types are exported."""
        assert hasattr(bbb, 'AzurePath')
        assert hasattr(bbb, 'Stat')
        assert hasattr(bbb, 'DirEntry')
        assert hasattr(bbb, 'WriteMode')

    def test_module_level_functions_exist(self):
        """Test that expected module-level functions exist (matching native boostedblob exports)."""
        # Path/stat functions at module level
        expected_funcs = [
            'exists', 'stat', 'getsize', 'isdir', 'isfile',
            'listdir', 'scandir', 'listtree', 'scantree',
            'copyfile', 'copytree', 'remove', 'rmtree', 'sync',
            'shutdown'
        ]
        for func_name in expected_funcs:
            assert hasattr(bbb, func_name), f"Module should export {func_name}"
        
        # Read/write functions via submodules (matching native boostedblob)
        assert hasattr(bbb.read, 'read_single')
        assert hasattr(bbb.read, 'read_byte_range')
        assert hasattr(bbb.write, 'write_single')


# ---------------------------------------------------------------
# Performance Tests
# ---------------------------------------------------------------

class TestPerformance:
    """Test performance characteristics."""

    def test_large_file_read_performance(self):
        """Test reading larger files works efficiently."""
        test_data = b"X" * (100 * 1024)  # 100KB
        blob_path = f"{AZURE_BLOB_PATH}/perf_large_{id(self)}.bin"
        
        run_async(bbb.write.write_single(blob_path, test_data))
        
        start_time = time.time()
        result = run_async(bbb.read.read_single(blob_path))
        read_time = time.time() - start_time
        
        assert len(result) == len(test_data)
        assert read_time < 60, f"Read took too long: {read_time}s"

    def test_multiple_small_reads(self):
        """Test multiple small file operations work correctly."""
        base_path = f"{AZURE_BLOB_PATH}/perf_multi_{id(self)}"
        num_files = 5
        
        # Write multiple small files
        for i in range(num_files):
            run_async(bbb.write.write_single(f"{base_path}/file_{i}.txt", f"Content {i}".encode()))
        
        # Read them back
        for i in range(num_files):
            result = run_async(bbb.read.read_single(f"{base_path}/file_{i}.txt"))
            assert result == f"Content {i}".encode()


# ---------------------------------------------------------------
# Edge Cases Tests
# ---------------------------------------------------------------

class TestEdgeCases:
    """Test edge cases and special scenarios."""

    def test_empty_file_handling(self):
        """Test empty file handling."""
        blob_path = f"{AZURE_BLOB_PATH}/edge_empty_{id(self)}.txt"
        run_async(bbb.write.write_single(blob_path, b""))
        
        result = run_async(bbb.read.read_single(blob_path))
        assert result == b""
        assert run_async(bbb.getsize(blob_path)) == 0

    def test_unicode_content(self):
        """Test Unicode content roundtrip."""
        blob_path = f"{AZURE_BLOB_PATH}/edge_unicode_{id(self)}.txt"
        test_data = "Unicode: 你好世界 🌍 Ёмодзи café"
        
        run_async(bbb.write.write_single(blob_path, test_data))
        result = run_async(bbb.read.read_single(blob_path))
        assert result.decode('utf-8') == test_data

    def test_special_characters_in_content(self):
        """Test special characters in file content."""
        test_data = "Special: \t\n\r\\\"'`${}[]<>!@#%^&*()"
        blob_path = f"{AZURE_BLOB_PATH}/edge_special_{id(self)}.txt"
        
        run_async(bbb.write.write_single(blob_path, test_data.encode()))
        result = run_async(bbb.read.read_single(blob_path))
        assert result.decode('utf-8') == test_data

    def test_binary_null_bytes(self):
        """Test binary content with null bytes."""
        blob_path = f"{AZURE_BLOB_PATH}/edge_nullbytes_{id(self)}.bin"
        test_data = b"\x00\x01\x02\x00\x00\xff\xfe\x00"
        
        run_async(bbb.write.write_single(blob_path, test_data))
        result = run_async(bbb.read.read_single(blob_path))
        assert result == test_data

    def test_very_long_path(self):
        """Test handling of long blob paths."""
        nested_parts = "/".join([f"level{i}" for i in range(10)])
        blob_path = f"{AZURE_BLOB_PATH}/{nested_parts}/file.txt"
        
        run_async(bbb.write.write_single(blob_path, b"deep content"))
        result = run_async(bbb.read.read_single(blob_path))
        assert result == b"deep content"

    def test_path_with_spaces(self):
        """Test handling of paths with spaces."""
        blob_path = f"{AZURE_BLOB_PATH}/path with spaces/file name.txt"
        
        run_async(bbb.write.write_single(blob_path, b"spaced content"))
        result = run_async(bbb.read.read_single(blob_path))
        assert result == b"spaced content"
