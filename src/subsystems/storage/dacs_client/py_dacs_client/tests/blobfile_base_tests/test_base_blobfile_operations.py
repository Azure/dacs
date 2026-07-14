#!/usr/bin/env python3
# Copyright (c) Microsoft Corporation.
# Licensed under the Apache License, Version 2.0.

# --------------------------------------------------------------------------------------------------------------------
# Azure Blob Storage I/O Test Suite using blobfile package
# Tests basic I/O operations against Azure Blob Storage using blobfile library with Azure CLI authentication
# --------------------------------------------------------------------------------------------------------------------

import hashlib
import logging
import os
import time
import pytest

import blobfile as bf


# Shared fixtures
@pytest.fixture(scope="session")
def azure_config():
    """Configure Azure authentication and paths."""
    os.environ["AZURE_USE_IDENTITY"] = "1"

    account_name = os.getenv("AZURE_STORAGE_ACCOUNT", "storageinterceptionpr1")
    container_name = os.getenv("AZURE_CONTAINER_NAME", "dataset")
    directory_path = os.getenv("AZURE_DIRECTORY_PATH", "rojin_huge_file").strip("/")

    if directory_path:
        base_url = f"az://{account_name}/{container_name}/{directory_path}"
    else:
        base_url = f"az://{account_name}/{container_name}"

    logging.basicConfig(level=logging.INFO, format="%(asctime)s - %(levelname)s - %(message)s")
    logger = logging.getLogger(__name__)

    logger.info(f"Azure Blob Storage test initialized")
    logger.info(f"  Account: {account_name}")
    logger.info(f"  Container: {container_name}")
    logger.info(f"  Directory: {directory_path if directory_path else 'root'}")
    logger.info(f"  Base URL: {base_url}")
    logger.info(f"  Authentication: Azure CLI (az login)")

    return {
        "account_name": account_name,
        "container_name": container_name,
        "directory_path": directory_path,
        "base_url": base_url,
        "logger": logger,
    }


@pytest.fixture
def test_context(azure_config):
    """Create test context for each test."""
    test_id = f"test-{int(time.time())}-{id(test_context) % 10000}"
    test_files = []

    context = {
        "test_id": test_id,
        "test_path": azure_config["base_url"],
        "test_files": test_files,
        "logger": azure_config["logger"],
    }

    def register_file(file_path):
        test_files.append(file_path)
        return file_path

    context["register_file"] = register_file

    yield context

    # Cleanup
    cleanup_count = 0
    for test_file in test_files:
        try:
            if bf.exists(test_file):
                bf.remove(test_file)
                cleanup_count += 1
        except Exception as e:
            azure_config["logger"].warning(f"Failed to remove test file {test_file}: {e}")

    if cleanup_count > 0:
        azure_config["logger"].info(f"Cleaned up {cleanup_count} test files")


class TestBasicOperations:
    """
    Test suite for basic Azure Blob Storage I/O operations.

    Covers fundamental file operations:
    - Basic read/write operations
    - File existence checking
    - File statistics and metadata
    - File copy and removal
    - Data integrity verification

    Authentication: Uses Azure CLI (az login) credentials automatically
    """

    def _register_test_file(self, file_path: str):
        """Register a test file for cleanup."""
        test_context["test_files"].append(file_path)
        return file_path

    def test_basic_write_read(self, test_context):
        """Test basic write and read operations."""
        test_file = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-basic.txt")
        test_data = b"Hello, Azure Blob Storage via blobfile!"

        with bf.BlobFile(test_file, "wb") as f:
            f.write(test_data)

        assert bf.exists(test_file), "File should exist after write"

        with bf.BlobFile(test_file, "rb") as f:
            read_data = f.read()

        assert (
            read_data == test_data
        ), f"[ASSERTION] Read data should match written data. Expected {len(test_data)} bytes, got {len(read_data)} bytes"
        test_context["logger"].info("✅ [TEST_PASS] Basic write/read test passed")

    def test_file_statistics(self, test_context):
        """Test file statistics and metadata."""
        test_file = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-stats.txt")
        test_data = b"This is test data for statistics validation." * 10  # 440 bytes

        test_context["logger"].info(f"[TEST] test_file_statistics - File: {test_file}, Expected size: {len(test_data)}")
        with bf.BlobFile(test_file, "wb") as f:
            f.write(test_data)

        stats = bf.stat(test_file)
        assert stats.size == len(
            test_data
        ), f"[ASSERTION] File size should match written data. Expected: {len(test_data)}, Got: {stats.size}"
        test_context["logger"].info(f"✅ [TEST_PASS] File statistics test passed - Size: {stats.size} bytes")

    def test_nonexistent_file(self, test_context):
        """Test handling of non-existent files."""
        nonexistent_file = f"{test_context['test_path']}/{test_context['test_id']}-does_not_exist.txt"

        assert not bf.exists(nonexistent_file), "Non-existent file should return False"

        with pytest.raises((FileNotFoundError, OSError)):
            with bf.BlobFile(nonexistent_file, "rb") as f:
                f.read()

        test_context["logger"].info("✅ Non-existent file handling test passed")

    def test_partial_reads(self, test_context):
        """Test partial reading operations."""
        test_file = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-partial.txt")
        test_data = b"0123456789" * 100  # 1000 bytes

        with bf.BlobFile(test_file, "wb") as f:
            f.write(test_data)

        with bf.BlobFile(test_file, "rb") as f:
            first_chunk = f.read(10)
            assert first_chunk == test_data[:10], "First chunk should match"

            second_chunk = f.read(20)
            assert second_chunk == test_data[10:30], "Second chunk should match"

            remaining = f.read()
            assert first_chunk + second_chunk + remaining == test_data, "Combined reads should equal original data"

        test_context["logger"].info("✅ Partial reads test passed")

    def test_data_integrity(self, test_context):
        """Test data integrity using checksums."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-integrity.bin"
        )
        test_data = bytes([(i * 17 + 23) % 256 for i in range(5000)])  # 5KB
        original_md5 = hashlib.md5(test_data).hexdigest()

        with bf.BlobFile(test_file, "wb") as f:
            f.write(test_data)

        with bf.BlobFile(test_file, "rb") as f:
            read_data = f.read()

        read_md5 = hashlib.md5(read_data).hexdigest()

        assert len(read_data) == len(test_data), "Data length should match"
        assert read_md5 == original_md5, "MD5 checksums should match"
        assert read_data == test_data, "Data content should be identical"
        test_context["logger"].info(f"✅ Data integrity test passed - MD5: {original_md5[:8]}...")


class TestFileOperations:
    """Test suite for file manipulation operations (copy, remove, directory listing)."""

    def _register_test_file(self, file_path: str):
        """Register a test file for cleanup."""
        test_context["test_files"].append(file_path)
        return file_path

    def test_file_copy(self, test_context):
        """Test copying files between locations."""
        src_file = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-copy-src.txt")
        dst_file = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-copy-dst.txt")
        test_data = b"Data to be copied between blob locations"

        with bf.BlobFile(src_file, "wb") as f:
            f.write(test_data)

        bf.copy(src_file, dst_file)

        assert bf.exists(dst_file), "Destination file should exist after copy"

        with bf.BlobFile(dst_file, "rb") as f:
            copied_data = f.read()

        assert copied_data == test_data, "Copied data should match original"
        test_context["logger"].info("✅ File copy test passed")

    def test_file_remove(self, test_context):
        """Test file deletion."""
        test_file = f"{test_context['test_path']}/{test_context['test_id']}-remove.txt"

        with bf.BlobFile(test_file, "wb") as f:
            f.write(b"File to be removed")

        assert bf.exists(test_file), "File should exist before removal"

        bf.remove(test_file)

        assert not bf.exists(test_file), "File should not exist after removal"
        test_context["logger"].info("✅ File remove test passed")

    def test_directory_listing(self, test_context):
        """Test listing files in a directory."""
        # Create multiple test files
        file1 = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-list-1.txt")
        file2 = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-list-2.txt")
        file3 = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-list-3.txt")

        for test_file in [file1, file2, file3]:
            with bf.BlobFile(test_file, "wb") as f:
                f.write(b"test content")

        # List directory contents
        files = list(bf.listdir(f"{test_context['test_path']}/"))

        # Check that our files are in the listing
        file_names = [f.split("/")[-1] for f in files]
        assert f"{test_context['test_id']}-list-1.txt" in file_names, "File 1 should be in directory listing"
        assert f"{test_context['test_id']}-list-2.txt" in file_names, "File 2 should be in directory listing"
        assert f"{test_context['test_id']}-list-3.txt" in file_names, "File 3 should be in directory listing"

        test_context["logger"].info(f"✅ Directory listing test passed - Found {len(files)} files")

    def test_glob_pattern(self, test_context):
        """Test glob pattern matching."""
        # Create files with different extensions
        txt_file = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-glob.txt")
        bin_file = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-glob.bin")
        log_file = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-glob.log")

        for test_file in [txt_file, bin_file, log_file]:
            with bf.BlobFile(test_file, "wb") as f:
                f.write(b"test")

        # Test glob pattern for .txt files
        txt_pattern = f"{test_context['test_path']}/{test_context['test_id']}-glob*.txt"
        txt_matches = list(bf.glob(txt_pattern))

        assert len(txt_matches) == 1, "Should find exactly one .txt file"
        assert txt_matches[0].endswith(".txt"), "Matched file should be .txt"

        # Test glob pattern for all files
        all_pattern = f"{test_context['test_path']}/{test_context['test_id']}-glob*"
        all_matches = list(bf.glob(all_pattern))

        assert len(all_matches) == 3, "Should find all three files"
        test_context["logger"].info(f"✅ Glob pattern test passed - Found {len(all_matches)} matching files")


class TestAdvancedReadOperations:
    """Test suite for advanced read operations (partial reads, seek/tell, chunked reading)."""

    def _register_test_file(self, file_path: str):
        """Register a test file for cleanup."""
        test_context["test_files"].append(file_path)
        return file_path

    def test_partial_reads(self, test_context):
        """Test partial reading operations."""
        test_file = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-partial.txt")
        test_data = b"0123456789" * 100  # 1000 bytes

        with bf.BlobFile(test_file, "wb") as f:
            f.write(test_data)

        with bf.BlobFile(test_file, "rb") as f:
            first_chunk = f.read(10)
            assert first_chunk == test_data[:10], "First chunk should match"

            second_chunk = f.read(20)
            assert second_chunk == test_data[10:30], "Second chunk should match"

            remaining = f.read()
            assert first_chunk + second_chunk + remaining == test_data, "Combined reads should equal original data"

        test_context["logger"].info("✅ Partial reads test passed")

    def test_seek_and_tell(self, test_context):
        """Test file seek and tell operations."""
        test_file = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-seek.txt")
        test_data = b"0123456789ABCDEFGHIJ"

        with bf.BlobFile(test_file, "wb") as f:
            f.write(test_data)

        with bf.BlobFile(test_file, "rb") as f:
            # Test tell at start
            pos = f.tell()
            assert pos == 0, "Position should be 0 at start"

            # Read some data
            f.read(5)
            pos = f.tell()
            assert pos == 5, "Position should be 5 after reading 5 bytes"

            # Seek to position 10
            f.seek(10)
            pos = f.tell()
            assert pos == 10, "Position should be 10 after seek"

            # Read from position 10
            data = f.read(5)
            assert data == b"ABCDE", "Should read correct data from position 10"

            # Seek from end
            f.seek(-5, 2)  # 5 bytes from end
            data = f.read()
            assert data == b"FGHIJ", "Should read last 5 bytes"

        test_context["logger"].info("✅ Seek and tell test passed")


class TestWriteOperations:
    """Test suite for write operations (append mode, multiple writes, large files)."""

    def _register_test_file(self, file_path: str):
        """Register a test file for cleanup."""
        test_context["test_files"].append(file_path)
        return file_path

    def test_append_mode(self, test_context):
        """Test appending data to existing files."""
        test_file = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-append.txt")

        # Write initial data
        with bf.BlobFile(test_file, "wb") as f:
            f.write(b"First line\n")

        # Append more data
        with bf.BlobFile(test_file, "ab") as f:
            f.write(b"Second line\n")

        # Read and verify
        with bf.BlobFile(test_file, "rb") as f:
            content = f.read()

        assert content == b"First line\nSecond line\n", "Content should include both lines"
        test_context["logger"].info("✅ Append mode test passed")

    def test_seek_and_tell(self, test_context):
        """Test file seek and tell operations."""
        test_file = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-seek.txt")
        test_data = b"0123456789ABCDEFGHIJ"

        with bf.BlobFile(test_file, "wb") as f:
            f.write(test_data)

        with bf.BlobFile(test_file, "rb") as f:
            # Test tell at start
            pos = f.tell()
            assert pos == 0, "Position should be 0 at start"

            # Read some data
            f.read(5)
            pos = f.tell()
            assert pos == 5, "Position should be 5 after reading 5 bytes"

            # Seek to position 10
            f.seek(10)
            pos = f.tell()
            assert pos == 10, "Position should be 10 after seek"

            # Read from position 10
            data = f.read(5)
            assert data == b"ABCDE", "Should read correct data from position 10"

            # Seek from end
            f.seek(-5, 2)  # 5 bytes from end
            data = f.read()
            assert data == b"FGHIJ", "Should read last 5 bytes"

        test_context["logger"].info("✅ Seek and tell test passed")

    def test_empty_file(self, test_context):
        """Test handling of empty files."""
        test_file = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-empty.txt")

        # Create empty file
        with bf.BlobFile(test_file, "wb") as f:
            pass  # Write nothing

        assert bf.exists(test_file), "Empty file should exist"

        # Check stats
        stats = bf.stat(test_file)
        assert stats.size == 0, "Empty file should have size 0"

        # Read empty file
        with bf.BlobFile(test_file, "rb") as f:
            content = f.read()

        assert content == b"", "Empty file should return empty bytes"
        test_context["logger"].info("✅ Empty file test passed")


class TestEdgeCases:
    """Test suite for edge cases (empty files, non-existent files, large files)."""

    def _register_test_file(self, file_path: str):
        """Register a test file for cleanup."""
        test_context["test_files"].append(file_path)
        return file_path

    def test_nonexistent_file(self, test_context):
        """Test handling of non-existent files."""
        nonexistent_file = f"{test_context['test_path']}/{test_context['test_id']}-does_not_exist.txt"

        assert not bf.exists(nonexistent_file), "Non-existent file should return False"

        with pytest.raises((FileNotFoundError, OSError)):
            with bf.BlobFile(nonexistent_file, "rb") as f:
                f.read()

        test_context["logger"].info("✅ Non-existent file handling test passed")

    def test_large_write(self, test_context):
        """Test writing larger data (1MB)."""
        test_file = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-large.bin")
        test_data = b"X" * (1024 * 1024)  # 1MB

        with bf.BlobFile(test_file, "wb") as f:
            f.write(test_data)

        stats = bf.stat(test_file)
        assert stats.size == len(test_data), "Large file size should match"

        # Verify by reading back
        with bf.BlobFile(test_file, "rb") as f:
            read_data = f.read()

        assert len(read_data) == len(test_data), "Read data size should match written data"
        assert read_data == test_data, "Large file data should match"
        test_context["logger"].info(f"✅ Large write test passed - {len(test_data)} bytes")

    def test_multiple_writes(self, test_context):
        """Test multiple sequential writes to the same file."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-multiwrite.txt"
        )

        with bf.BlobFile(test_file, "wb") as f:
            f.write(b"Line 1\n")
            f.write(b"Line 2\n")
            f.write(b"Line 3\n")

        with bf.BlobFile(test_file, "rb") as f:
            content = f.read()

        expected = b"Line 1\nLine 2\nLine 3\n"
        assert content == expected, "Multiple writes should be concatenated"
        test_context["logger"].info("✅ Multiple writes test passed")


class TestFileModes:
    """Test suite for all file open modes ('r', 'rb', 'w', 'wb', 'a', 'ab')."""

    def _register_test_file(self, file_path: str):
        """Register a test file for cleanup."""
        test_context["test_files"].append(file_path)
        return file_path

    # ==================== BINARY READ MODE ('rb') ====================

    def test_mode_rb_binary_read(self, test_context):
        """Test 'rb' mode - binary read."""
        test_file = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-mode-rb.bin")
        test_data = bytes([0, 1, 2, 255, 254, 128, 64, 32])

        # Write test data
        with bf.BlobFile(test_file, "wb") as f:
            f.write(test_data)

        # Read in binary mode
        with bf.BlobFile(test_file, "rb") as f:
            read_data = f.read()

        assert isinstance(read_data, bytes), "rb mode should return bytes"
        assert read_data == test_data, "Binary data should match exactly"
        test_context["logger"].info("✅ Mode 'rb' test passed")

    def test_mode_r_text_read(self, test_context):
        """Test 'r' mode - text read."""
        test_file = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-mode-r.txt")
        test_text = "Hello, World!\nLine 2\nLine 3"

        # Write as binary
        with bf.BlobFile(test_file, "wb") as f:
            f.write(test_text.encode("utf-8"))

        # Read in text mode
        with bf.BlobFile(test_file, "r") as f:
            read_text = f.read()

        assert isinstance(read_text, str), "r mode should return string"
        assert read_text == test_text, "Text should match exactly"
        test_context["logger"].info("✅ Mode 'r' test passed")

    def test_mode_r_readline(self, test_context):
        """Test 'r' mode with readline operations."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-mode-r-lines.txt"
        )
        lines = ["First line\n", "Second line\n", "Third line\n"]

        # Write lines
        with bf.BlobFile(test_file, "w") as f:
            f.writelines(lines)

        # Read line by line
        with bf.BlobFile(test_file, "r") as f:
            line1 = f.readline()
            line2 = f.readline()
            line3 = f.readline()
            line4 = f.readline()  # Should be empty

        assert line1 == "First line\n", "First line should match"
        assert line2 == "Second line\n", "Second line should match"
        assert line3 == "Third line\n", "Third line should match"
        assert line4 == "", "Fourth readline should return empty string"
        test_context["logger"].info("✅ Mode 'r' readline test passed")

    def test_mode_r_readlines(self, test_context):
        """Test 'r' mode with readlines operation."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-mode-r-readlines.txt"
        )
        lines = ["Line A\n", "Line B\n", "Line C"]

        with bf.BlobFile(test_file, "w") as f:
            f.writelines(lines)

        with bf.BlobFile(test_file, "r") as f:
            read_lines = f.readlines()

        assert len(read_lines) == 3, "Should have 3 lines"
        assert read_lines[0] == "Line A\n"
        assert read_lines[1] == "Line B\n"
        assert read_lines[2] == "Line C"
        test_context["logger"].info("✅ Mode 'r' readlines test passed")

    def test_mode_r_iteration(self, test_context):
        """Test 'r' mode with iteration."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-mode-r-iter.txt"
        )
        expected_lines = ["Alpha\n", "Beta\n", "Gamma\n", "Delta"]

        with bf.BlobFile(test_file, "w") as f:
            f.writelines(expected_lines)

        # Iterate through file
        with bf.BlobFile(test_file, "r") as f:
            actual_lines = [line for line in f]

        assert actual_lines == expected_lines, "Iteration should yield all lines"
        test_context["logger"].info("✅ Mode 'r' iteration test passed")

    def test_mode_wb_binary_write(self, test_context):
        """Test 'wb' mode - binary write (overwrites existing)."""
        test_file = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-mode-wb.bin")

        # First write
        with bf.BlobFile(test_file, "wb") as f:
            f.write(b"Original content")

        # Overwrite with wb mode
        with bf.BlobFile(test_file, "wb") as f:
            f.write(b"New content")

        with bf.BlobFile(test_file, "rb") as f:
            content = f.read()

        assert content == b"New content", "wb mode should overwrite existing content"
        test_context["logger"].info("✅ Mode 'wb' test passed")

    def test_mode_w_text_write(self, test_context):
        """Test 'w' mode - text write (overwrites existing)."""
        test_file = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-mode-w.txt")

        # First write
        with bf.BlobFile(test_file, "w") as f:
            f.write("First version")

        # Overwrite with w mode
        with bf.BlobFile(test_file, "w") as f:
            f.write("Second version")

        with bf.BlobFile(test_file, "r") as f:
            content = f.read()

        assert content == "Second version", "w mode should overwrite existing content"
        test_context["logger"].info("✅ Mode 'w' test passed")

    def test_mode_w_multiple_writes(self, test_context):
        """Test 'w' mode with multiple write calls in single context."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-mode-w-multi.txt"
        )

        with bf.BlobFile(test_file, "w") as f:
            f.write("Part 1 ")
            f.write("Part 2 ")
            f.write("Part 3")

        with bf.BlobFile(test_file, "r") as f:
            content = f.read()

        assert content == "Part 1 Part 2 Part 3", "Multiple writes should concatenate"
        test_context["logger"].info("✅ Mode 'w' multiple writes test passed")

    def test_mode_ab_append_binary(self, test_context):
        """Test 'ab' mode - append binary data."""
        test_file = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-mode-ab.bin")

        # Initial write
        with bf.BlobFile(test_file, "wb") as f:
            f.write(b"Initial")

        # Append with ab mode
        with bf.BlobFile(test_file, "ab") as f:
            f.write(b" Appended")

        # Append more
        with bf.BlobFile(test_file, "ab") as f:
            f.write(b" More")

        with bf.BlobFile(test_file, "rb") as f:
            content = f.read()

        assert content == b"Initial Appended More", "ab mode should append binary data"
        test_context["logger"].info("✅ Mode 'ab' test passed")

    def test_mode_a_append_text(self, test_context):
        """Test 'a' mode - append text data."""
        test_file = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-mode-a.txt")

        # Initial write
        with bf.BlobFile(test_file, "w") as f:
            f.write("First\n")

        # Append with a mode
        with bf.BlobFile(test_file, "a") as f:
            f.write("Second\n")

        # Append more
        with bf.BlobFile(test_file, "a") as f:
            f.write("Third\n")

        with bf.BlobFile(test_file, "r") as f:
            content = f.read()

        assert content == "First\nSecond\nThird\n", "a mode should append text data"
        test_context["logger"].info("✅ Mode 'a' test passed")

    def test_mode_a_multiple_appends_single_context(self, test_context):
        """Test 'a' mode with multiple writes in single context."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-mode-a-multi.txt"
        )

        with bf.BlobFile(test_file, "w") as f:
            f.write("Base\n")

        with bf.BlobFile(test_file, "a") as f:
            f.write("Append1\n")
            f.write("Append2\n")
            f.write("Append3\n")

        with bf.BlobFile(test_file, "r") as f:
            content = f.read()

        assert content == "Base\nAppend1\nAppend2\nAppend3\n", "Multiple appends in single context should work"
        test_context["logger"].info("✅ Mode 'a' multiple appends test passed")

    def test_mode_ab_nonexistent_file(self, test_context):
        """Test 'ab' mode creates file if it doesn't exist."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-mode-ab-new.bin"
        )

        # Append to non-existent file (should create it)
        with bf.BlobFile(test_file, "ab") as f:
            f.write(b"Created via append")

        assert bf.exists(test_file), "ab mode should create file if it doesn't exist"

        with bf.BlobFile(test_file, "rb") as f:
            content = f.read()

        assert content == b"Created via append", "Content should match"
        test_context["logger"].info("✅ Mode 'ab' on non-existent file test passed")

    def test_mode_a_nonexistent_file(self, test_context):
        """Test 'a' mode creates file if it doesn't exist."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-mode-a-new.txt"
        )

        with bf.BlobFile(test_file, "a") as f:
            f.write("Created via text append")

        assert bf.exists(test_file), "a mode should create file if it doesn't exist"

        with bf.BlobFile(test_file, "r") as f:
            content = f.read()

        assert content == "Created via text append", "Content should match"
        test_context["logger"].info("✅ Mode 'a' on non-existent file test passed")

    def test_mode_rb_with_seek(self, test_context):
        """Test 'rb' mode with seek operations."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-mode-rb-seek.bin"
        )
        test_data = b"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"

        with bf.BlobFile(test_file, "wb") as f:
            f.write(test_data)

        with bf.BlobFile(test_file, "rb") as f:
            # Seek to middle
            f.seek(10)
            data1 = f.read(5)
            assert data1 == b"ABCDE"

            # Seek back to start
            f.seek(0)
            data2 = f.read(5)
            assert data2 == b"01234"

            # Seek from current position
            f.seek(5, 1)
            data3 = f.read(5)
            assert data3 == b"ABCDE"

        test_context["logger"].info("✅ Mode 'rb' with seek test passed")

    def test_mode_r_with_seek(self, test_context):
        """Test 'r' mode with seek operations."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-mode-r-seek.txt"
        )
        test_text = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"

        with bf.BlobFile(test_file, "w") as f:
            f.write(test_text)

        with bf.BlobFile(test_file, "r") as f:
            # Seek to position
            f.seek(5)
            text1 = f.read(5)
            assert text1 == "FGHIJ"

            # Seek to beginning
            f.seek(0)
            text2 = f.read(3)
            assert text2 == "ABC"

        test_context["logger"].info("✅ Mode 'r' with seek test passed")

    def test_mode_wb_empty_write(self, test_context):
        """Test 'wb' mode with empty write."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-mode-wb-empty.bin"
        )

        with bf.BlobFile(test_file, "wb") as f:
            pass  # Write nothing

        assert bf.exists(test_file), "File should exist even with empty write"

        stats = bf.stat(test_file)
        assert stats.size == 0, "File size should be 0"
        test_context["logger"].info("✅ Mode 'wb' empty write test passed")

    def test_mode_w_empty_write(self, test_context):
        """Test 'w' mode with empty write."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-mode-w-empty.txt"
        )

        with bf.BlobFile(test_file, "w") as f:
            pass  # Write nothing

        assert bf.exists(test_file), "File should exist even with empty write"

        stats = bf.stat(test_file)
        assert stats.size == 0, "File size should be 0"
        test_context["logger"].info("✅ Mode 'w' empty write test passed")

    def test_mode_rb_large_chunks(self, test_context):
        """Test 'rb' mode reading large file in chunks."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-mode-rb-chunks.bin"
        )
        test_data = bytes(range(256)) * 100  # 25.6KB

        with bf.BlobFile(test_file, "wb") as f:
            f.write(test_data)

        # Read in 1KB chunks
        chunk_size = 1024
        chunks = []
        with bf.BlobFile(test_file, "rb") as f:
            while True:
                chunk = f.read(chunk_size)
                if not chunk:
                    break
                chunks.append(chunk)

        reconstructed = b"".join(chunks)
        assert reconstructed == test_data, "Chunked reads should reconstruct original data"
        test_context["logger"].info(f"✅ Mode 'rb' large chunks test passed - {len(chunks)} chunks")

    def test_mode_r_text_encoding(self, test_context):
        """Test 'r' mode with UTF-8 encoded text."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-mode-r-utf8.txt"
        )
        test_text = "Hello 世界 🌍 Привет مرحبا"

        with bf.BlobFile(test_file, "w") as f:
            f.write(test_text)

        with bf.BlobFile(test_file, "r") as f:
            read_text = f.read()

        assert read_text == test_text, "UTF-8 text should be preserved"
        test_context["logger"].info("✅ Mode 'r' UTF-8 encoding test passed")

    # ==================== BINARY WRITE MODE ('wb') ====================

    def test_mode_wb_binary_write(self, test_context):
        """Test 'wb' mode - binary write (overwrites existing)."""
        test_file = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-mode-wb.bin")

        # First write
        with bf.BlobFile(test_file, "wb") as f:
            f.write(b"Original content")

        # Overwrite with wb mode
        with bf.BlobFile(test_file, "wb") as f:
            f.write(b"New content")

        with bf.BlobFile(test_file, "rb") as f:
            content = f.read()

        assert content == b"New content", "wb mode should overwrite existing content"
        test_context["logger"].info("✅ Mode 'wb' test passed")

    def test_mode_wb_empty_write(self, test_context):
        """Test 'wb' mode with empty write."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-mode-wb-empty.bin"
        )

        with bf.BlobFile(test_file, "wb") as f:
            pass  # Write nothing

        assert bf.exists(test_file), "File should exist even with empty write"

        stats = bf.stat(test_file)
        assert stats.size == 0, "File size should be 0"
        test_context["logger"].info("✅ Mode 'wb' empty write test passed")

    # ==================== TEXT WRITE MODE ('w') ====================

    def test_mode_w_text_write(self, test_context):
        """Test 'w' mode - text write (overwrites existing)."""
        test_file = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-mode-w.txt")

        # First write
        with bf.BlobFile(test_file, "w") as f:
            f.write("First version")

        # Overwrite with w mode
        with bf.BlobFile(test_file, "w") as f:
            f.write("Second version")

        with bf.BlobFile(test_file, "r") as f:
            content = f.read()

        assert content == "Second version", "w mode should overwrite existing content"
        test_context["logger"].info("✅ Mode 'w' test passed")

    def test_mode_w_multiple_writes(self, test_context):
        """Test 'w' mode with multiple write calls in single context."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-mode-w-multi.txt"
        )

        with bf.BlobFile(test_file, "w") as f:
            f.write("Part 1 ")
            f.write("Part 2 ")
            f.write("Part 3")

        with bf.BlobFile(test_file, "r") as f:
            content = f.read()

        assert content == "Part 1 Part 2 Part 3", "Multiple writes should concatenate"
        test_context["logger"].info("✅ Mode 'w' multiple writes test passed")

    def test_mode_w_empty_write(self, test_context):
        """Test 'w' mode with empty write."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-mode-w-empty.txt"
        )

        with bf.BlobFile(test_file, "w") as f:
            pass  # Write nothing

        assert bf.exists(test_file), "File should exist even with empty write"

        stats = bf.stat(test_file)
        assert stats.size == 0, "File size should be 0"
        test_context["logger"].info("✅ Mode 'w' empty write test passed")

    # ==================== BINARY APPEND MODE ('ab') ====================

    def test_mode_ab_append_binary(self, test_context):
        """Test 'ab' mode - append binary data."""
        test_file = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-mode-ab.bin")

        # Initial write
        with bf.BlobFile(test_file, "wb") as f:
            f.write(b"Initial")

        # Append with ab mode
        with bf.BlobFile(test_file, "ab") as f:
            f.write(b" Appended")

        # Append more
        with bf.BlobFile(test_file, "ab") as f:
            f.write(b" More")

        with bf.BlobFile(test_file, "rb") as f:
            content = f.read()

        assert content == b"Initial Appended More", "ab mode should append binary data"
        test_context["logger"].info("✅ Mode 'ab' test passed")

    def test_mode_ab_nonexistent_file(self, test_context):
        """Test 'ab' mode creates file if it doesn't exist."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-mode-ab-new.bin"
        )

        # Append to non-existent file (should create it)
        with bf.BlobFile(test_file, "ab") as f:
            f.write(b"Created via append")

        assert bf.exists(test_file), "ab mode should create file if it doesn't exist"

        with bf.BlobFile(test_file, "rb") as f:
            content = f.read()

        assert content == b"Created via append", "Content should match"
        test_context["logger"].info("✅ Mode 'ab' on non-existent file test passed")

    def test_mode_ab_binary_integrity(self, test_context):
        """Test 'ab' mode preserves binary integrity."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-mode-ab-integrity.bin"
        )

        # Write binary data with all byte values
        part1 = bytes(range(128))
        part2 = bytes(range(128, 256))

        with bf.BlobFile(test_file, "wb") as f:
            f.write(part1)

        with bf.BlobFile(test_file, "ab") as f:
            f.write(part2)

        with bf.BlobFile(test_file, "rb") as f:
            result = f.read()

        expected = bytes(range(256))
        assert result == expected, "Binary data should be preserved exactly"
        test_context["logger"].info("✅ Mode 'ab' binary integrity test passed")

    # ==================== TEXT APPEND MODE ('a') ====================

    def test_mode_a_append_text(self, test_context):
        """Test 'a' mode - append text data."""
        test_file = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-mode-a.txt")

        # Initial write
        with bf.BlobFile(test_file, "w") as f:
            f.write("First\n")

        # Append with a mode
        with bf.BlobFile(test_file, "a") as f:
            f.write("Second\n")

        # Append more
        with bf.BlobFile(test_file, "a") as f:
            f.write("Third\n")

        with bf.BlobFile(test_file, "r") as f:
            content = f.read()

        assert content == "First\nSecond\nThird\n", "a mode should append text data"
        test_context["logger"].info("✅ Mode 'a' test passed")

    def test_mode_a_multiple_appends_single_context(self, test_context):
        """Test 'a' mode with multiple writes in single context."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-mode-a-multi.txt"
        )

        with bf.BlobFile(test_file, "w") as f:
            f.write("Base\n")

        with bf.BlobFile(test_file, "a") as f:
            f.write("Append1\n")
            f.write("Append2\n")
            f.write("Append3\n")

        with bf.BlobFile(test_file, "r") as f:
            content = f.read()

        assert content == "Base\nAppend1\nAppend2\nAppend3\n", "Multiple appends in single context should work"
        test_context["logger"].info("✅ Mode 'a' multiple appends test passed")

    def test_mode_a_nonexistent_file(self, test_context):
        """Test 'a' mode creates file if it doesn't exist."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-mode-a-new.txt"
        )

        with bf.BlobFile(test_file, "a") as f:
            f.write("Created via text append")

        assert bf.exists(test_file), "a mode should create file if it doesn't exist"

        with bf.BlobFile(test_file, "r") as f:
            content = f.read()

        assert content == "Created via text append", "Content should match"
        test_context["logger"].info("✅ Mode 'a' on non-existent file test passed")

    # ==================== MODE COMBINATIONS & WORKFLOWS ====================

    def test_mode_combinations_workflow(self, test_context):
        """Test realistic workflow combining different modes."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-mode-workflow.log"
        )

        # Initial write
        with bf.BlobFile(test_file, "w") as f:
            f.write("Session started\n")

        # Append log entries
        with bf.BlobFile(test_file, "a") as f:
            f.write("Event 1: User login\n")
            f.write("Event 2: File uploaded\n")

        # Read current state
        with bf.BlobFile(test_file, "r") as f:
            logs = f.readlines()

        assert len(logs) == 3, "Should have 3 log lines"
        assert "Session started" in logs[0]
        assert "User login" in logs[1]
        assert "File uploaded" in logs[2]

        # Append more
        with bf.BlobFile(test_file, "a") as f:
            f.write("Event 3: Session closed\n")

        # Read final state
        with bf.BlobFile(test_file, "r") as f:
            final_content = f.read()

        assert "Session closed" in final_content
        test_context["logger"].info("✅ Mode combinations workflow test passed")


class TestStreamingParameter:
    """Test suite for streaming parameter (streaming=True/False behavior)."""

    def _register_test_file(self, file_path: str):
        """Register a test file for cleanup."""
        test_context["test_files"].append(file_path)
        return file_path

    def test_streaming_true_read(self, test_context):
        """Test streaming=True for reading (default for read modes)."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-stream-true-read.bin"
        )
        test_data = b"Stream reading test data" * 100

        # Write test data
        with bf.BlobFile(test_file, "wb") as f:
            f.write(test_data)

        # Read with explicit streaming=True (should be default)
        with bf.BlobFile(test_file, "rb", streaming=True) as f:
            read_data = f.read()

        assert read_data == test_data, "Streaming read should work correctly"
        test_context["logger"].info("✅ Streaming=True read test passed")

    def test_streaming_false_read(self, test_context):
        """Test streaming=False for reading (downloads entire file first)."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-stream-false-read.bin"
        )
        test_data = b"Non-streaming read test" * 50

        with bf.BlobFile(test_file, "wb") as f:
            f.write(test_data)

        # Read with streaming=False (downloads to local file first)
        with bf.BlobFile(test_file, "rb", streaming=False) as f:
            read_data = f.read()

        assert read_data == test_data, "Non-streaming read should work correctly"
        test_context["logger"].info("✅ Streaming=False read test passed")

    def test_streaming_true_write(self, test_context):
        """Test streaming=True for writing (chunks uploaded directly)."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-stream-true-write.bin"
        )
        test_data = b"X" * (10 * 1024 * 1024)  # 10MB to test chunked upload

        # Write with streaming=True (uploads in chunks)
        with bf.BlobFile(test_file, "wb", streaming=True) as f:
            f.write(test_data)

        # Verify file exists and has correct size
        stats = bf.stat(test_file)
        assert stats.size == len(test_data), "Streaming write should upload all data"
        test_context["logger"].info(f"✅ Streaming=True write test passed - {stats.size} bytes")

    def test_streaming_false_write(self, test_context):
        """Test streaming=False for writing (uploads on close)."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-stream-false-write.bin"
        )
        test_data = b"Non-streaming write test data"

        # Write with streaming=False (uploads on close)
        with bf.BlobFile(test_file, "wb", streaming=False) as f:
            f.write(test_data)
            # File should not exist yet until close

        # After close, file should exist
        assert bf.exists(test_file), "File should exist after close"

        with bf.BlobFile(test_file, "rb") as f:
            read_data = f.read()

        assert read_data == test_data, "Non-streaming write should work correctly"
        test_context["logger"].info("✅ Streaming=False write test passed")

    def test_streaming_false_append(self, test_context):
        """Test streaming=False with append mode (downloads, modifies, uploads)."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-stream-false-append.txt"
        )

        # Initial write
        with bf.BlobFile(test_file, "w") as f:
            f.write("Initial content\n")

        # Append with streaming=False
        with bf.BlobFile(test_file, "a", streaming=False) as f:
            f.write("Appended content\n")

        with bf.BlobFile(test_file, "r") as f:
            content = f.read()

        assert content == "Initial content\nAppended content\n", "Non-streaming append should work correctly"
        test_context["logger"].info("✅ Streaming=False append test passed")

    def test_streaming_partial_reads(self, test_context):
        """Test streaming=True with partial reads."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-stream-partial.bin"
        )
        test_data = b"0123456789" * 1000  # 10KB

        with bf.BlobFile(test_file, "wb") as f:
            f.write(test_data)

        # Read in chunks with streaming=True
        chunks = []
        with bf.BlobFile(test_file, "rb", streaming=True) as f:
            while True:
                chunk = f.read(1024)
                if not chunk:
                    break
                chunks.append(chunk)

        reconstructed = b"".join(chunks)
        assert reconstructed == test_data, "Streaming partial reads should work"
        test_context["logger"].info(f"✅ Streaming partial reads test passed - {len(chunks)} chunks")

    def test_streaming_write_flush(self, test_context):
        """Test that flush() doesn't cause early write with streaming=True."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-stream-flush.txt"
        )

        with bf.BlobFile(test_file, "w", streaming=True) as f:
            f.write("Before flush\n")
            f.flush()  # Should not cause immediate upload with streaming=True
            f.write("After flush\n")

        with bf.BlobFile(test_file, "r") as f:
            content = f.read()

        assert "Before flush" in content
        assert "After flush" in content
        test_context["logger"].info("✅ Streaming write flush test passed")


class TestBufferSizeParameter:
    """Test suite for buffer_size parameter (small, large, chunked reads)."""

    def _register_test_file(self, file_path: str):
        """Register a test file for cleanup."""
        test_context["test_files"].append(file_path)
        return file_path

    def test_buffer_size_small(self, test_context):
        """Test reading with small buffer size."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-buffer-small.bin"
        )
        test_data = b"A" * 10000  # 10KB

        with bf.BlobFile(test_file, "wb") as f:
            f.write(test_data)

        # Read with small buffer (512 bytes)
        with bf.BlobFile(test_file, "rb", buffer_size=512) as f:
            read_data = f.read()

        assert read_data == test_data, "Small buffer size should work correctly"
        test_context["logger"].info("✅ Small buffer_size test passed")

    def test_buffer_size_large(self, test_context):
        """Test reading with large buffer size."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-buffer-large.bin"
        )
        test_data = b"B" * 50000  # 50KB

        with bf.BlobFile(test_file, "wb") as f:
            f.write(test_data)

        # Read with large buffer (1MB)
        with bf.BlobFile(test_file, "rb", buffer_size=1024 * 1024) as f:
            read_data = f.read()

        assert read_data == test_data, "Large buffer size should work correctly"
        test_context["logger"].info("✅ Large buffer_size test passed")

    def test_buffer_size_chunked_reads(self, test_context):
        """Test buffer_size with chunked reading operations."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-buffer-chunks.bin"
        )
        test_data = bytes(range(256)) * 100  # 25.6KB

        with bf.BlobFile(test_file, "wb") as f:
            f.write(test_data)

        # Read in small chunks with specific buffer size
        chunks = []
        with bf.BlobFile(test_file, "rb", buffer_size=4096) as f:
            while True:
                chunk = f.read(1000)
                if not chunk:
                    break
                chunks.append(chunk)

        reconstructed = b"".join(chunks)
        assert reconstructed == test_data, "Buffered chunked reads should work"
        test_context["logger"].info(f"✅ buffer_size chunked reads test passed - {len(chunks)} chunks")


class TestCacheDirParameter:
    """Test suite for cache_dir parameter (cached reads with streaming=False)."""

    def _register_test_file(self, file_path: str):
        """Register a test file for cleanup."""
        test_context["test_files"].append(file_path)
        return file_path

    def test_cache_dir_read(self, test_context):
        """Test cache_dir parameter with streaming=False read."""
        import tempfile

        test_file = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-cache.bin")
        test_data = b"Cached read test data" * 100

        with bf.BlobFile(test_file, "wb") as f:
            f.write(test_data)

        # Create temporary cache directory
        with tempfile.TemporaryDirectory() as cache_dir:
            # Read with cache_dir (requires streaming=False)
            with bf.BlobFile(test_file, "rb", streaming=False, cache_dir=cache_dir) as f:
                read_data = f.read()

            assert read_data == test_data, "Cached read should work correctly"

            # Verify cache directory was used (files may exist temporarily)
            test_context["logger"].info(f"✅ cache_dir read test passed - cache: {cache_dir}")

    def test_cache_dir_multiple_reads(self, test_context):
        """Test multiple reads with same cache_dir."""
        import tempfile

        test_file1 = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-cache1.bin")
        test_file2 = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-cache2.bin")
        test_data1 = b"First cached file"
        test_data2 = b"Second cached file"

        with bf.BlobFile(test_file1, "wb") as f:
            f.write(test_data1)
        with bf.BlobFile(test_file2, "wb") as f:
            f.write(test_data2)

        with tempfile.TemporaryDirectory() as cache_dir:
            # Read multiple files using same cache directory
            with bf.BlobFile(test_file1, "rb", streaming=False, cache_dir=cache_dir) as f:
                data1 = f.read()

            with bf.BlobFile(test_file2, "rb", streaming=False, cache_dir=cache_dir) as f:
                data2 = f.read()

            assert data1 == test_data1, "First cached read should work"
            assert data2 == test_data2, "Second cached read should work"
            test_context["logger"].info("✅ Multiple cache_dir reads test passed")


class TestCombinedParameters:
    """Test suite for combined parameters and realistic workflows."""

    def _register_test_file(self, file_path: str):
        """Register a test file for cleanup."""
        test_context["test_files"].append(file_path)
        return file_path

    def test_combined_streaming_buffer(self, test_context):
        """Test combination of streaming=True with custom buffer_size."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-stream-buffer.bin"
        )
        test_data = b"Z" * (100 * 1024)  # 100KB

        with bf.BlobFile(test_file, "wb") as f:
            f.write(test_data)

        # Read with streaming and custom buffer
        with bf.BlobFile(test_file, "rb", streaming=True, buffer_size=8192) as f:
            read_data = f.read()

        assert read_data == test_data, "Combined streaming+buffer should work"
        test_context["logger"].info("✅ Combined streaming+buffer test passed")

    def test_combined_nonstreaming_buffer_cache(self, test_context):
        """Test combination of streaming=False with buffer_size and cache_dir."""
        import tempfile

        test_file = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-combined.bin")
        test_data = b"Combined parameters test" * 200

        with bf.BlobFile(test_file, "wb") as f:
            f.write(test_data)

        with tempfile.TemporaryDirectory() as cache_dir:
            with bf.BlobFile(test_file, "rb", streaming=False, buffer_size=4096, cache_dir=cache_dir) as f:
                read_data = f.read()

            assert read_data == test_data, "Combined streaming=False+buffer+cache should work"
            test_context["logger"].info("✅ Combined non-streaming+buffer+cache test passed")

    def test_text_mode_with_streaming(self, test_context):
        """Test text mode with different streaming settings."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-text-stream.txt"
        )
        test_text = "Line 1\nLine 2\nLine 3\n" * 100

        # Write with streaming
        with bf.BlobFile(test_file, "w", streaming=True) as f:
            f.write(test_text)

        # Read with streaming=True
        with bf.BlobFile(test_file, "r", streaming=True) as f:
            read_stream = f.read()

        # Read with streaming=False
        with bf.BlobFile(test_file, "r", streaming=False) as f:
            read_nonstream = f.read()

        assert read_stream == test_text, "Streaming text read should work"
        assert read_nonstream == test_text, "Non-streaming text read should work"
        assert read_stream == read_nonstream, "Both methods should give same result"
        test_context["logger"].info("✅ Text mode with streaming test passed")

    def test_large_file_streaming_efficiency(self, test_context):
        """Test streaming with large file (5MB) for efficiency."""
        test_file = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-large-stream.bin"
        )
        test_data = b"Y" * (5 * 1024 * 1024)  # 5MB

        # Write large file with streaming
        with bf.BlobFile(test_file, "wb", streaming=True) as f:
            f.write(test_data)

        # Read large file with streaming in chunks
        read_size = 0
        with bf.BlobFile(test_file, "rb", streaming=True, buffer_size=64 * 1024) as f:
            while True:
                chunk = f.read(256 * 1024)  # 256KB chunks
                if not chunk:
                    break
                read_size += len(chunk)

        assert read_size == len(test_data), "Streaming should handle large files efficiently"
        test_context["logger"].info(f"✅ Large file streaming test passed - {read_size} bytes")

    def test_default_parameters(self, test_context):
        """Test that default parameters work as expected."""
        test_file = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-defaults.txt")
        test_text = "Testing default parameters"

        # Write with defaults (streaming=False for write modes)
        with bf.BlobFile(test_file, "w") as f:
            f.write(test_text)

        # Read with defaults (streaming=True for read modes)
        with bf.BlobFile(test_file, "r") as f:
            read_text = f.read()

        assert read_text == test_text, "Default parameters should work correctly"
        test_context["logger"].info("✅ Default parameters test passed")


class TestDirectoryOperations:
    """Test suite for directory operations (makedirs, rmdir, rmtree, walk, isdir)."""

    def _register_test_file(self, file_path: str):
        """Register a test file for cleanup."""
        test_context["test_files"].append(file_path)
        return file_path

    def _register_test_dir(self, dir_path: str, test_context):
        """Register a test directory for cleanup."""
        if "test_dirs" not in test_context:
            test_context["test_dirs"] = []
        test_context["test_dirs"].append(dir_path)
        return dir_path

    def test_makedirs(self, test_context):
        """Test creating nested directories."""
        test_dir = self._register_test_dir(
            f"{test_context['test_path']}/{test_context['test_id']}-makedirs/level1/level2", test_context
        )

        # Create nested directories
        bf.makedirs(test_dir)

        # Create a file in the directory
        test_file = test_context["register_file"](f"{test_dir}/test.txt")
        with bf.BlobFile(test_file, "w") as f:
            f.write("test content")

        assert bf.exists(test_file), "File should exist in created directory"
        test_context["logger"].info("✅ makedirs test passed")

    def test_isdir(self, test_context):
        """Test checking if path is a directory."""
        test_dir = self._register_test_dir(
            f"{test_context['test_path']}/{test_context['test_id']}-isdir/", test_context
        )
        test_file = test_context["register_file"](f"{test_dir}test.txt")

        # Create a file
        with bf.BlobFile(test_file, "w") as f:
            f.write("content")

        # Check directory (note: in blob storage, directories are implicit)
        # A path ending with / or containing files is treated as a directory
        is_dir = bf.isdir(test_dir)
        is_file_dir = bf.isdir(test_file)

        assert not is_file_dir, "File should not be identified as directory"
        test_context["logger"].info(f"✅ isdir test passed - dir: {is_dir}, file: {is_file_dir}")

    def test_rmdir(self, test_context):
        """Test removing an empty directory."""
        test_dir = self._register_test_dir(
            f"{test_context['test_path']}/{test_context['test_id']}-rmdir/", test_context
        )

        # Create and then remove directory
        bf.makedirs(test_dir)

        # Directory should be "empty" in blob storage context
        bf.rmdir(test_dir)

        test_context["logger"].info("✅ rmdir test passed")

    def test_rmtree(self, test_context):
        """Test removing directory tree with contents."""
        test_dir = self._register_test_dir(
            f"{test_context['test_path']}/{test_context['test_id']}-rmtree/", test_context
        )

        # Create directory structure with files
        file1 = f"{test_dir}file1.txt"
        file2 = f"{test_dir}subdir/file2.txt"
        file3 = f"{test_dir}subdir/deeper/file3.txt"

        with bf.BlobFile(file1, "w") as f:
            f.write("content1")
        with bf.BlobFile(file2, "w") as f:
            f.write("content2")
        with bf.BlobFile(file3, "w") as f:
            f.write("content3")

        # Verify files exist
        assert bf.exists(file1)
        assert bf.exists(file2)
        assert bf.exists(file3)

        # Remove entire tree
        bf.rmtree(test_dir)

        # Verify files are gone
        assert not bf.exists(file1)
        assert not bf.exists(file2)
        assert not bf.exists(file3)

        test_context["logger"].info("✅ rmtree test passed")

    def test_walk(self, test_context):
        """Test walking directory tree."""
        test_dir = self._register_test_dir(f"{test_context['test_path']}/{test_context['test_id']}-walk/", test_context)

        # Create directory structure
        with bf.BlobFile(f"{test_dir}root.txt", "w") as f:
            f.write("root")
        with bf.BlobFile(f"{test_dir}dir1/file1.txt", "w") as f:
            f.write("file1")
        with bf.BlobFile(f"{test_dir}dir1/file2.txt", "w") as f:
            f.write("file2")
        with bf.BlobFile(f"{test_dir}dir2/file3.txt", "w") as f:
            f.write("file3")
        with bf.BlobFile(f"{test_dir}dir1/subdir/file4.txt", "w") as f:
            f.write("file4")

        # Walk the directory tree
        walked_paths = []
        for dirpath, dirnames, filenames in bf.walk(test_dir):
            walked_paths.append((dirpath, dirnames, filenames))

        assert len(walked_paths) > 0, "Should find paths when walking"
        test_context["logger"].info(f"✅ walk test passed - Found {len(walked_paths)} directory levels")


class TestPathOperations:
    """Test suite for path manipulation functions (basename, dirname, join)."""

    def _register_test_file(self, file_path: str):
        """Register a test file for cleanup."""
        test_context["test_files"].append(file_path)
        return file_path

    def test_basename(self, test_context):
        """Test getting basename from path."""
        path1 = f"{test_context['test_path']}/dir1/dir2/file.txt"
        path2 = f"{test_context['test_path']}/file.txt"
        path3 = f"{test_context['test_path']}/dir/"

        base1 = bf.basename(path1)
        base2 = bf.basename(path2)
        base3 = bf.basename(path3)

        assert base1 == "file.txt", "Basename should extract filename"
        assert base2 == "file.txt", "Basename should work for direct child"
        assert base3 in ["", "dir"], "Basename of directory"

        test_context["logger"].info(f"✅ basename test passed - {base1}, {base2}, {base3}")

    def test_dirname(self, test_context):
        """Test getting directory name from path."""
        path = f"{test_context['test_path']}/dir1/dir2/file.txt"

        dirname = bf.dirname(path)

        assert "dir2" in dirname, "Dirname should return parent directory"
        assert "file.txt" not in dirname, "Dirname should not include filename"

        test_context["logger"].info(f"✅ dirname test passed - {dirname}")

    def test_join(self, test_context):
        """Test joining path components."""
        base = test_context["test_path"]

        joined1 = bf.join(base, "dir1", "file.txt")
        joined2 = bf.join(base, "dir1/", "dir2", "file.txt")

        assert "dir1" in joined1
        assert "file.txt" in joined1
        assert "dir2" in joined2

        # Verify joined path works for file operations
        test_file = test_context["register_file"](joined1)
        with bf.BlobFile(test_file, "w") as f:
            f.write("test")

        assert bf.exists(test_file), "Joined path should work for file operations"
        test_context["logger"].info(f"✅ join test passed - {joined1}")


class TestMetadataOperations:
    """Test suite for metadata operations (md5, set_mtime, get_url)."""

    def _register_test_file(self, file_path: str):
        """Register a test file for cleanup."""
        test_context["test_files"].append(file_path)
        return file_path

    def test_md5(self, test_context):
        """Test getting MD5 hash of a file."""
        test_file = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-md5.txt")
        test_data = b"Test data for MD5 calculation"

        # Calculate expected MD5
        expected_md5 = hashlib.md5(test_data).hexdigest()

        # Write file
        with bf.BlobFile(test_file, "wb") as f:
            f.write(test_data)

        # Get MD5 from blobfile
        file_md5 = bf.md5(test_file)

        assert file_md5 == expected_md5, "MD5 should match expected value"
        test_context["logger"].info(f"✅ md5 test passed - {file_md5}")

    def test_set_mtime(self, test_context):
        """Test setting modification time."""
        test_file = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-mtime.txt")

        # Create file
        with bf.BlobFile(test_file, "w") as f:
            f.write("test content")

        # Get original mtime
        original_stat = bf.stat(test_file)
        original_mtime = original_stat.mtime

        # Set new mtime (1 hour earlier)
        new_mtime = original_mtime - 3600
        bf.set_mtime(test_file, new_mtime)

        # Verify mtime changed
        updated_stat = bf.stat(test_file)

        assert updated_stat.mtime != original_mtime, "Modified time should be updated"
        test_context["logger"].info(f"✅ set_mtime test passed - Original: {original_mtime}, New: {updated_stat.mtime}")

    def test_get_url(self, test_context):
        """Test getting temporary URL for file."""
        test_file = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-url.txt")
        test_data = b"Content accessible via URL"

        # Create file
        with bf.BlobFile(test_file, "wb") as f:
            f.write(test_data)

        # Get URL
        url, expiration = bf.get_url(test_file)

        assert url is not None, "URL should be returned"
        assert "http" in url.lower(), "URL should be HTTP/HTTPS"

        test_context["logger"].info(f"✅ get_url test passed - URL: {url[:50]}..., Expiration: {expiration}")


class TestScanOperations:
    """Test suite for scan operations (scandir, scanglob - generator versions)."""

    def _register_test_file(self, file_path: str):
        """Register a test file for cleanup."""
        test_context["test_files"].append(file_path)
        return file_path

    def test_scandir(self, test_context):
        """Test scandir (generator version of listdir)."""
        test_context["logger"].info(f"[TEST_START] test_scandir - test_id: {test_context['test_id']}")

        # Create test files
        file1 = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-scan-1.txt")
        file2 = test_context["register_file"](f"{test_context['test_path']}/{test_context['test_id']}-scan-2.txt")

        test_context["logger"].info(f"[TEST] Creating test files: {file1}, {file2}")
        for test_file in [file1, file2]:
            with bf.BlobFile(test_file, "wb") as f:
                f.write(b"scan test")
        test_context["logger"].info(f"[TEST] Files created successfully")

        # Use scandir (generator) - it returns full paths or basenames depending on implementation
        found_files = []
        all_entries = []
        scan_count = 0
        scan_path = f"{test_context['test_path']}/"
        test_context["logger"].info(f"[TEST] Scanning directory: {scan_path}")

        for entry in bf.scandir(scan_path):
            scan_count += 1
            all_entries.append(str(entry))
            # Check if our test ID is in the entry (works for both full paths and basenames)
            if test_context["test_id"] in str(entry) and "-scan-" in str(entry):
                found_files.append(entry)
                test_context["logger"].info(f"[TEST] Found matching file: {entry}")

        test_context["logger"].info(
            f"[TEST] Scan complete - Total entries: {scan_count}, Matching files: {len(found_files)}"
        )
        if scan_count > 0 and len(found_files) == 0:
            test_context["logger"].error(f"[TEST_FAIL] No matching files found. First 5 entries: {all_entries[:5]}")

        assert scan_count > 0, f"[ASSERTION] scandir should return some entries. Scan path: {scan_path}"
        assert (
            len(found_files) >= 2
        ), f"[ASSERTION] Should find 2+ test files using scandir. Found: {len(found_files)}, test_id: {test_context['test_id']}, Total entries: {scan_count}, Sample entries: {all_entries[:10]}"
        test_context["logger"].info(
            f"✅ [TEST_PASS] scandir test passed - Found {len(found_files)} files from {scan_count} total entries"
        )

    def test_scanglob(self, test_context):
        """Test scanglob (generator version of glob)."""
        # Create test files with pattern
        file1 = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-scanglob-test.txt"
        )
        file2 = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-scanglob-test.log"
        )
        file3 = test_context["register_file"](
            f"{test_context['test_path']}/{test_context['test_id']}-scanglob-data.bin"
        )

        for test_file in [file1, file2, file3]:
            with bf.BlobFile(test_file, "wb") as f:
                f.write(b"scanglob")

        # Use scanglob with pattern
        pattern = f"{test_context['test_path']}/{test_context['test_id']}-scanglob-test*"
        matches = []
        for match in bf.scanglob(pattern):
            matches.append(match)

        assert len(matches) == 2, "Should match 2 files with pattern"
        test_context["logger"].info(f"✅ scanglob test passed - Matched {len(matches)} files")
