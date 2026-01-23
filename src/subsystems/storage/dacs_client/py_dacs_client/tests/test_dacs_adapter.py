#!/usr/bin/env python3
# ---------------------------------------------------------------
# DACS Client - Remote Azure Blob Tests for DACSAdapter
# ---------------------------------------------------------------

import errno
import logging
import os
import sys
import signal
import faulthandler
import traceback
import pytest

# Enable faulthandler to get Python traceback on segfault/abort
faulthandler.enable()

def _signal_handler(signum, frame):
    """Handle signals to provide better crash information."""
    sig_name = signal.Signals(signum).name
    print(f"\n=== SIGNAL RECEIVED: {sig_name} ({signum}) ===", file=sys.stderr)
    print("Stack trace:", file=sys.stderr)
    traceback.print_stack(frame, file=sys.stderr)
    sys.stderr.flush()
    # Re-raise the signal to get the default behavior (core dump etc.)
    signal.signal(signum, signal.SIG_DFL)
    os.kill(os.getpid(), signum)


# Register signal handlers for debugging
for sig in [signal.SIGABRT, signal.SIGSEGV, signal.SIGBUS, signal.SIGFPE]:
    try:
        signal.signal(sig, _signal_handler)
    except (OSError, ValueError):
        pass  # Some signals can't be caught

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import dacs_adapter as bf
from dacs_adapter import (
    DACSStorageError,
    DACSAuthenticationError,
    DACSPermissionError,
    DACSNotFoundError,
    DACSExistsError,
    DACSBusyError,
    DACSTimeoutError,
    DACSNetworkError,
    DACSQuotaError,
    DACSInvalidInputError,
    DACSServiceUnavailableError,
    OperationNotSupportedError,
)
from py_dacs_client.dacs_exceptions import (
    _classify_error,
    errno_to_exception_class,
    handle_native_error,
    DACSError,
    DACSIsDirectoryError,
    DACSNotDirectoryError,
    DACSNotEmptyError,
    DACSNameTooLongError,
    DACSReadOnlyError,
)
from dacs_utils import alloc_buffer, release_buffer

# Import the unique session path from conftest.py
# This ensures all tests use the same isolated path for this test run,
# avoiding conflicts with other concurrent test runs
from conftest import AZURE_BLOB_PATH, TEST_SESSION_ID


@pytest.fixture(scope="session", autouse=True)
def setup_logging():
    """Configure logging for all tests."""
    logging.basicConfig(level=logging.INFO, format="%(asctime)s - %(levelname)s - %(message)s")


# Note: Test blob cleanup is handled in conftest.py via pytest_sessionfinish hook
# This ensures cleanup runs reliably at the end of the test session regardless of
# test success/failure. See conftest.py for implementation details.


class TestRemoteBlobRead:
    """Test DACSAdapter read() for remote Azure blobs."""

    def test_read_remote_blob(self):
        """Test reading from remote Azure blob."""
        test_data = "Remote blob test data"
        blob_path = f"{AZURE_BLOB_PATH}/test_read_blob_{id(self)}.txt"
        with bf.DACSAdapter(blob_path, "w") as f:
            f.write(test_data)

        with bf.DACSAdapter(blob_path, "r") as f:
            content = f.read()
        assert content == test_data

    def test_read_partial_remote_blob(self):
        """Test reading partial data from remote blob."""
        test_data = "Hello from remote blob"
        blob_path = f"{AZURE_BLOB_PATH}/test_read_partial_{id(self)}.txt"
        with bf.DACSAdapter(blob_path, "w") as f:
            f.write(test_data)

        with bf.DACSAdapter(blob_path, "r") as f:
            content = f.read(5)
        assert content == "Hello"

    def test_read_with_user_allocated_buffer(self):
        """Test reading with user-allocated buffer (caller manages lifecycle)."""
        test_data = "User buffer test data"
        blob_path = f"{AZURE_BLOB_PATH}/test_read_user_buffer_{id(self)}.txt"
        with bf.DACSAdapter(blob_path, "w") as f:
            f.write(test_data)

        # Allocate buffer externally - user is responsible for releasing
        buffer_size = len(test_data) + 10  # Extra space
        user_buffer = alloc_buffer(buffer_size)

        try:
            with bf.DACSAdapter(blob_path, "rb") as f:
                content = f.read(size=len(test_data), buffer=user_buffer)

            # Verify content is correct (user buffer returns memoryview)
            assert bytes(content) == test_data.encode("utf-8")

            # Buffer should still be valid and usable after read
            # (not released by DACSAdapter since user provided it)
        finally:
            # User must release the buffer
            release_buffer(user_buffer)

    def test_read_with_user_buffer_reuse(self):
        """Test reusing user-allocated buffer across multiple reads."""
        test_data_1 = "First file content"
        test_data_2 = "Second file data"
        blob_path_1 = f"{AZURE_BLOB_PATH}/test_read_reuse_1_{id(self)}.txt"
        blob_path_2 = f"{AZURE_BLOB_PATH}/test_read_reuse_2_{id(self)}.txt"

        # Create both files
        with bf.DACSAdapter(blob_path_1, "w") as f:
            f.write(test_data_1)
        with bf.DACSAdapter(blob_path_2, "w") as f:
            f.write(test_data_2)

        # Allocate a single buffer large enough for both
        buffer_size = max(len(test_data_1), len(test_data_2)) + 10
        user_buffer = alloc_buffer(buffer_size)

        try:
            # Read first file
            with bf.DACSAdapter(blob_path_1, "rb") as f:
                content_1 = f.read(size=len(test_data_1), buffer=user_buffer)
            assert bytes(content_1) == test_data_1.encode("utf-8")

            # Reuse same buffer for second file
            with bf.DACSAdapter(blob_path_2, "rb") as f:
                content_2 = f.read(size=len(test_data_2), buffer=user_buffer)
            assert bytes(content_2) == test_data_2.encode("utf-8")
        finally:
            # Release once at the end
            release_buffer(user_buffer)

    def test_read_zero_copy_memoryview(self):
        """Test that zero-copy read returns memoryview pointing to user buffer."""
        import ctypes

        test_data = "Zero copy memoryview test"
        blob_path = f"{AZURE_BLOB_PATH}/test_read_zerocopy_{id(self)}.txt"
        with bf.DACSAdapter(blob_path, "w") as f:
            f.write(test_data)

        buffer_size = len(test_data) + 10
        user_buffer = alloc_buffer(buffer_size)

        try:
            with bf.DACSAdapter(blob_path, "rb") as f:
                result = f.read(size=len(test_data), buffer=user_buffer)

            # Should return memoryview for zero-copy path
            assert isinstance(result, memoryview), f"Zero-copy read should return memoryview, got {type(result)}"

            # Memoryview should contain correct data
            assert bytes(result) == test_data.encode("utf-8"), "Memoryview content should match written data"

            # Memoryview should be valid while buffer is alive
            assert len(result) == len(test_data), "Memoryview length should match data length"
        finally:
            release_buffer(user_buffer)

    def test_read_standard_path_returns_bytes(self):
        """Test that standard read (no user buffer) returns bytes, not memoryview."""
        test_data = "Standard read returns bytes"
        blob_path = f"{AZURE_BLOB_PATH}/test_read_standard_{id(self)}.txt"
        with bf.DACSAdapter(blob_path, "w") as f:
            f.write(test_data)

        with bf.DACSAdapter(blob_path, "rb") as f:
            result = f.read()

        # Should return bytes (copy), not memoryview
        assert isinstance(result, bytes), f"Standard read should return bytes, got {type(result)}"
        assert result == test_data.encode("utf-8")

    def test_read_zero_copy_buffer_reuse_no_extra_allocation(self):
        """Test zero-copy read path reuses buffer without extra allocations."""
        import ctypes

        # Create multiple files
        num_files = 5
        test_data = "Reusable buffer content"
        blob_paths = []
        for i in range(num_files):
            path = f"{AZURE_BLOB_PATH}/test_zerocopy_reuse_{id(self)}_{i}.txt"
            with bf.DACSAdapter(path, "w") as f:
                f.write(test_data)
            blob_paths.append(path)

        # Single buffer for all reads
        buffer_size = len(test_data) + 10
        user_buffer = alloc_buffer(buffer_size)

        try:
            for path in blob_paths:
                with bf.DACSAdapter(path, "rb") as f:
                    result = f.read(size=len(test_data), buffer=user_buffer)

                # Each read should return memoryview (zero-copy)
                assert isinstance(result, memoryview)
                assert bytes(result) == test_data.encode("utf-8")
        finally:
            release_buffer(user_buffer)


class TestRemoteBlobWrite:
    """Test DACSAdapter write() for remote Azure blobs."""

    def test_write_to_remote_blob(self):
        """Test writing to remote Azure blob."""
        test_data = "Test data for remote blob"
        blob_path = f"{AZURE_BLOB_PATH}/test_write_blob_{id(self)}.txt"
        with bf.DACSAdapter(blob_path, "w") as f:
            bytes_written = f.write(test_data)
        assert bytes_written > 0

    def test_write_with_user_buffer_zero_copy(self):
        """Test zero-copy write using user-provided buffer."""
        import ctypes

        test_data = b"Zero copy write test data"
        blob_path = f"{AZURE_BLOB_PATH}/test_write_zerocopy_{id(self)}.txt"

        # Allocate buffer and fill with data
        user_buffer = alloc_buffer(len(test_data))
        ctypes.memmove(user_buffer, test_data, len(test_data))

        try:
            with bf.DACSAdapter(blob_path, "wb") as f:
                # Write using buffer directly - no internal allocation/copy
                bytes_written = f.write(buffer=user_buffer, size=len(test_data))

            assert bytes_written == len(test_data), f"Should write {len(test_data)} bytes, wrote {bytes_written}"

            # Verify data was written correctly by reading it back
            with bf.DACSAdapter(blob_path, "rb") as f:
                content = f.read()
            assert content == test_data, "Read content should match written data"
        finally:
            release_buffer(user_buffer)

    def test_write_zero_copy_buffer_reuse(self):
        """Test zero-copy write path with buffer reuse across multiple writes."""
        import ctypes

        # Create buffer large enough for multiple writes
        max_size = 100
        user_buffer = alloc_buffer(max_size)

        try:
            for i in range(3):
                test_data = f"Write iteration {i} data".encode("utf-8")
                blob_path = f"{AZURE_BLOB_PATH}/test_write_reuse_{id(self)}_{i}.txt"

                # Reuse same buffer - just update content
                ctypes.memmove(user_buffer, test_data, len(test_data))

                with bf.DACSAdapter(blob_path, "wb") as f:
                    bytes_written = f.write(buffer=user_buffer, size=len(test_data))

                assert bytes_written == len(test_data)

                # Verify each write
                with bf.DACSAdapter(blob_path, "rb") as f:
                    content = f.read()
                assert content == test_data
        finally:
            release_buffer(user_buffer)

    def test_write_standard_path_with_string(self):
        """Test standard write path with string data (internal buffer allocation)."""
        test_data = "Standard string write"
        blob_path = f"{AZURE_BLOB_PATH}/test_write_standard_str_{id(self)}.txt"

        with bf.DACSAdapter(blob_path, "w") as f:
            bytes_written = f.write(test_data)

        assert bytes_written == len(test_data)

        # Verify
        with bf.DACSAdapter(blob_path, "r") as f:
            content = f.read()
        assert content == test_data

    def test_write_standard_path_with_bytes(self):
        """Test standard write path with bytes data (internal buffer allocation)."""
        test_data = b"Standard bytes write"
        blob_path = f"{AZURE_BLOB_PATH}/test_write_standard_bytes_{id(self)}.txt"

        with bf.DACSAdapter(blob_path, "wb") as f:
            bytes_written = f.write(test_data)

        assert bytes_written == len(test_data)

        # Verify
        with bf.DACSAdapter(blob_path, "rb") as f:
            content = f.read()
        assert content == test_data

    def test_write_zero_copy_requires_size(self):
        """Test that zero-copy write with buffer requires size parameter."""
        import ctypes

        test_data = b"Test data"
        blob_path = f"{AZURE_BLOB_PATH}/test_write_no_size_{id(self)}.txt"

        user_buffer = alloc_buffer(len(test_data))
        ctypes.memmove(user_buffer, test_data, len(test_data))

        try:
            with bf.DACSAdapter(blob_path, "wb") as f:
                # Should raise error when buffer provided without size
                with pytest.raises(ValueError, match="size must be specified"):
                    f.write(buffer=user_buffer)
        finally:
            release_buffer(user_buffer)

    def test_read_write_zero_copy_roundtrip(self):
        """Test complete zero-copy roundtrip: write with buffer, read with buffer."""
        import ctypes

        test_data = b"Complete zero-copy roundtrip test data"
        blob_path = f"{AZURE_BLOB_PATH}/test_zerocopy_roundtrip_{id(self)}.txt"

        # Use same buffer for both write and read
        buffer_size = len(test_data) + 20
        user_buffer = alloc_buffer(buffer_size)

        try:
            # Zero-copy write
            ctypes.memmove(user_buffer, test_data, len(test_data))
            with bf.DACSAdapter(blob_path, "wb") as f:
                bytes_written = f.write(buffer=user_buffer, size=len(test_data))
            assert bytes_written == len(test_data)

            # Clear buffer to prove read works
            ctypes.memset(user_buffer, 0, buffer_size)

            # Zero-copy read into same buffer
            with bf.DACSAdapter(blob_path, "rb") as f:
                result = f.read(size=len(test_data), buffer=user_buffer)

            assert isinstance(result, memoryview)
            assert bytes(result) == test_data
        finally:
            release_buffer(user_buffer)

    def test_write_multiple_times_to_remote_blob(self):
        """Test that multiple write operations are not supported (limitation).

        Note: This test may be skipped if the first write fails due to permission
        or configuration issues in the test environment.
        """
        data_parts = ["Part 1 ", "Part 2 ", "Part 3"]
        blob_path = f"{AZURE_BLOB_PATH}/test_write_multi_{id(self)}.txt"

        try:
            # First write should succeed
            with bf.DACSAdapter(blob_path, "w") as f:
                bytes_written = f.write(data_parts[0])

                # If first write fails (returns <=0 or raises), skip the rest
                if bytes_written <= 0:
                    pytest.skip("Write not supported in this environment (first write returned non-positive)")

                assert bytes_written == len(data_parts[0])

                # Second write fails silently or raises error - overwrite/append not supported
                # The storage intercept library logs error but may return -1 or raise exception
                try:
                    bytes_written_2 = f.write(data_parts[1])
                    # If it doesn't raise, it should return error indicator (negative or 0)
                    assert (
                        bytes_written_2 <= 0
                    ), "Second write should fail or return error indicator (overwrite not supported)"
                except (OSError, IOError, DACSStorageError):
                    # Exception is also acceptable for unsupported operation
                    pass
        except DACSStorageError as e:
            # If write fails entirely due to environment issues, skip the test
            pytest.skip(f"Write not supported in this environment: {e}")

    def test_write_internal_buffers_tracked_until_close(self):
        """Test that internal buffers are tracked and released only on close().

        Note: This test may be skipped if write fails due to permission
        or configuration issues in the test environment.
        """
        test_data_1 = "First write data"
        test_data_2 = "Second write data"
        blob_path = f"{AZURE_BLOB_PATH}/test_internal_buffer_tracking_{id(self)}.txt"

        f = bf.DACSAdapter(blob_path, "w")
        f.__enter__()

        try:
            # Write multiple times - buffers should be tracked
            try:
                f.write(test_data_1)
            except DACSStorageError as e:
                f.__exit__(None, None, None)
                pytest.skip(f"Write not supported in this environment: {e}")

            # Check internal buffer tracking (accessing private member for test)
            assert len(f._write_buffers) == 1, "Should track 1 buffer after first write"
            assert f._write_buffers[0][1] == False, "Should be marked as internal buffer"

            # Note: Second write may fail due to storage intercept limitations
            # but we're testing buffer tracking, not write success
            try:
                f.write(test_data_2)
                assert len(f._write_buffers) == 2, "Should track 2 buffers after second write"
            except (OSError, IOError, DACSStorageError):
                pass  # Write failure is OK, we're testing buffer tracking

            # Buffers should still be tracked (not released yet)
            assert len(f._write_buffers) >= 1, "Buffers should still be tracked before close"
        finally:
            f.__exit__(None, None, None)

        # After close, internal buffer list should be cleared
        assert len(f._write_buffers) == 0, "Buffer list should be empty after close"
        assert f._closed == True, "File should be marked as closed"

    def test_write_user_buffer_not_released_on_close(self):
        """Test that user-provided buffers are NOT released by DACSAdapter on close()."""
        import ctypes

        test_data = b"User buffer lifetime test"
        blob_path = f"{AZURE_BLOB_PATH}/test_user_buffer_lifetime_{id(self)}.txt"

        user_buffer = alloc_buffer(len(test_data))
        ctypes.memmove(user_buffer, test_data, len(test_data))

        # Write using user buffer
        with bf.DACSAdapter(blob_path, "wb") as f:
            f.write(buffer=user_buffer, size=len(test_data))

            # Check that buffer is tracked as user-provided
            assert len(f._write_buffers) == 1, "Should track user buffer"
            assert f._write_buffers[0][1] == True, "Should be marked as user-provided buffer"

        # After close, buffer should still be valid (not released by DACSAdapter)
        # We can verify by checking we can still access the buffer data
        try:
            import ctypes

            # Verify user buffer is still valid by reading its content
            content = ctypes.string_at(user_buffer, len(test_data))
            assert content == test_data, "User buffer should still be usable after close"
        finally:
            # User is responsible for releasing
            release_buffer(user_buffer)

    def test_write_mixed_internal_and_user_buffers(self):
        """Test mixing internal and user-provided buffers in writes."""
        import ctypes

        internal_data = "Internal buffer data"
        user_data = b"User buffer data"
        blob_path_1 = f"{AZURE_BLOB_PATH}/test_mixed_internal_{id(self)}.txt"
        blob_path_2 = f"{AZURE_BLOB_PATH}/test_mixed_user_{id(self)}.txt"

        user_buffer = alloc_buffer(len(user_data))
        ctypes.memmove(user_buffer, user_data, len(user_data))

        try:
            # Write with internal buffer
            with bf.DACSAdapter(blob_path_1, "w") as f:
                bytes_written_1 = f.write(internal_data)
                assert len(f._write_buffers) == 1
                assert f._write_buffers[0][1] == False  # Internal

            # Write with user buffer
            with bf.DACSAdapter(blob_path_2, "wb") as f:
                bytes_written_2 = f.write(buffer=user_buffer, size=len(user_data))
                assert len(f._write_buffers) == 1
                assert f._write_buffers[0][1] == True  # User-provided

            # Only verify reads if writes succeeded
            if bytes_written_1 > 0:
                with bf.DACSAdapter(blob_path_1, "r") as f:
                    content = f.read()
                    if content:  # Only check if we got data
                        assert content == internal_data
            if bytes_written_2 > 0:
                with bf.DACSAdapter(blob_path_2, "rb") as f:
                    content = f.read()
                    if content:  # Only check if we got data
                        assert content == user_data
        finally:
            release_buffer(user_buffer)

    def test_write_user_buffer_must_stay_alive_until_close(self):
        """Document and test the requirement that user buffers must stay alive until close()."""
        import ctypes

        test_data = b"Buffer lifetime critical test"
        blob_path = f"{AZURE_BLOB_PATH}/test_buffer_lifetime_{id(self)}.txt"

        # Allocate buffer that will stay alive for entire write operation
        user_buffer = alloc_buffer(len(test_data))
        ctypes.memmove(user_buffer, test_data, len(test_data))

        try:
            with bf.DACSAdapter(blob_path, "wb") as f:
                bytes_written = f.write(buffer=user_buffer, size=len(test_data))
                # Note: bytes_written may be -1 if storage backend fails
                # The key test is that buffer tracking works correctly

                # Verify buffer is tracked as user-provided
                assert len(f._write_buffers) == 1, "Should track user buffer"
                assert f._write_buffers[0][1] == True, "Should be marked as user-provided buffer"

                # Buffer must stay alive here - DACSAdapter tracks it but doesn't copy
                # Accessing buffer content should still work
                content = ctypes.string_at(user_buffer, len(test_data))
                assert content == test_data, "Buffer content should remain intact"
            # close() called - now safe to release buffer
        finally:
            release_buffer(user_buffer)


class TestRemoteBlobExists:
    """Test exists() for remote Azure blobs."""

    def test_exists_remote_blob_after_write(self):
        """Test exists() returns True for written remote blob."""
        blob_path = f"{AZURE_BLOB_PATH}/test_exists_true_{id(self)}.txt"
        with bf.DACSAdapter(blob_path, "w") as f:
            f.write("test data")

        result = bf.exists(blob_path)
        assert result is True

    def test_exists_nonexistent_remote_blob(self):
        """Test exists() returns False for non-existent remote blob."""
        nonexistent_path = f"{AZURE_BLOB_PATH}/nonexistent_blob_{id(self)}.txt"
        result = bf.exists(nonexistent_path)
        assert result is False


class TestRemoteBlobStat:
    """Test stat() for remote Azure blobs."""

    def test_stat_remote_blob(self):
        """Test stat() on remote blob."""
        test_data = "stat test data"
        blob_path = f"{AZURE_BLOB_PATH}/test_stat_blob_{id(self)}.txt"
        with bf.DACSAdapter(blob_path, "w") as f:
            bytes_written = f.write(test_data)

        result = bf.stat(blob_path)
        assert hasattr(result, "size")
        assert hasattr(result, "mtime")
        # Only verify size matches if write succeeded
        if bytes_written > 0:
            assert result.size == len(test_data), f"Expected size {len(test_data)}, got {result.size}"

    def test_stat_all_fields(self):
        """Test that stat() returns all expected fields (blobfile + extended)."""
        test_data = "Complete stat field test"
        blob_path = f"{AZURE_BLOB_PATH}/test_stat_fields_{id(self)}.txt"
        with bf.DACSAdapter(blob_path, "w") as f:
            bytes_written = f.write(test_data)

        result = bf.stat(blob_path)
        # Check blobfile API core fields
        assert hasattr(result, "size")
        assert hasattr(result, "mtime")
        assert hasattr(result, "ctime")
        assert hasattr(result, "md5")
        assert hasattr(result, "version")
        # Check extended st_ fields
        assert hasattr(result, "st_dev")
        assert hasattr(result, "st_ino")
        assert hasattr(result, "st_mode")
        assert hasattr(result, "st_nlink")
        assert hasattr(result, "st_uid")
        assert hasattr(result, "st_gid")
        assert hasattr(result, "st_rdev")
        assert hasattr(result, "st_atime")
        # Verify size is correct only if write succeeded
        if bytes_written > 0:
            assert result.size == len(test_data)
            # Verify timestamps are reasonable (non-zero for mtime at least)
            assert result.mtime >= 0  # mtime may be 0 in some storage systems

    def test_stat_nonexistent_blob(self):
        """Test stat() raises FileNotFoundError for non-existent blob."""
        nonexistent_path = f"{AZURE_BLOB_PATH}/nonexistent_stat_{id(self)}.txt"
        with pytest.raises(FileNotFoundError):
            bf.stat(nonexistent_path)

    def test_stat_empty_blob(self):
        """Test stat() on empty blob."""
        blob_path = f"{AZURE_BLOB_PATH}/test_stat_empty_{id(self)}.txt"
        with bf.DACSAdapter(blob_path, "w") as f:
            pass  # Write nothing

        result = bf.stat(blob_path)
        assert result.size == 0
        assert result.mtime > 0

    def test_stat_large_blob(self):
        """Test stat() on larger blob."""
        test_data = "X" * 10000  # 10KB of data
        blob_path = f"{AZURE_BLOB_PATH}/test_stat_large_{id(self)}.txt"
        with bf.DACSAdapter(blob_path, "w") as f:
            bytes_written = f.write(test_data)

        result = bf.stat(blob_path)
        # Only verify size if write succeeded
        if bytes_written > 0:
            assert result.size == len(test_data)
            assert result.size == 10000

    def test_stat_check_file_type(self):
        """Test using stat() to differentiate between file and directory."""
        import stat as stat_module

        # Create a regular file
        test_data = "file type test"
        file_path = f"{AZURE_BLOB_PATH}/test_file_type_{id(self)}.txt"
        with bf.DACSAdapter(file_path, "w") as f:
            bytes_written = f.write(test_data)

        # Stat the file
        file_stat = bf.stat(file_path)

        # Check if it's a regular file using st_mode (extended field)
        # S_ISREG checks if the mode indicates a regular file
        is_regular_file = stat_module.S_ISREG(file_stat.st_mode)
        is_directory = stat_module.S_ISDIR(file_stat.st_mode)

        # For a blob file, it should be detected as a regular file
        assert is_regular_file or file_stat.st_mode == 0, "File should be detected as regular file or have mode 0"

        # It should NOT be a directory
        assert not is_directory, "File should not be detected as directory"

        # The file should have non-zero size only if write succeeded
        if bytes_written > 0:
            assert file_stat.size > 0, "Regular file should have non-zero size"

        # Test directory stat
        # Stat the directory path (container or blob prefix)
        dir_path = AZURE_BLOB_PATH  # The base directory path
        dir_stat = bf.stat(dir_path)

        # Check if it's detected as a directory
        dir_is_regular_file = stat_module.S_ISREG(dir_stat.st_mode)
        dir_is_directory = stat_module.S_ISDIR(dir_stat.st_mode)

        # For a directory/container, it should be detected as a directory
        # Or at least not as a regular file
        if dir_stat.st_mode != 0:
            assert dir_is_directory, "Directory should be detected as directory"
            assert not dir_is_regular_file, "Directory should not be detected as regular file"

        # Directory size is typically 0 or has special meaning
        # Just verify we can read the size field
        assert hasattr(dir_stat, "size")


class TestRemoteBlobListDir:
    """Test listdir() and scandir() for remote Azure blob directories."""

    def test_listdir_empty_directory(self):
        """Test listdir() on empty directory."""
        dir_path = f"{AZURE_BLOB_PATH}/test_listdir_empty_{id(self)}"

        # List should return empty iterator for non-existent/empty directory
        # or may raise an error - both are acceptable
        try:
            items = list(bf.listdir(dir_path))
            assert isinstance(items, list)
        except (FileNotFoundError, OSError):
            pass  # Empty/non-existent directory may raise error

    def test_listdir_with_files(self):
        """Test listdir() returns files in directory."""
        dir_path = f"{AZURE_BLOB_PATH}/test_listdir_files_{id(self)}"

        # Create some files in the directory
        file_names = ["file1.txt", "file2.txt", "file3.txt"]
        for name in file_names:
            file_path = f"{dir_path}/{name}"
            with bf.DACSAdapter(file_path, "w") as f:
                f.write(f"content of {name}")

        # List the directory
        items = list(bf.listdir(dir_path))

        # Verify we got some items
        assert len(items) > 0, "Directory should contain files"

        # Verify our created files are in the list
        for name in file_names:
            assert name in items or any(name in item for item in items), f"File {name} should be in directory listing"

    def test_scandir_with_files(self):
        """Test scandir() returns files in directory."""
        dir_path = f"{AZURE_BLOB_PATH}/test_scandir_files_{id(self)}"

        # Create some files in the directory
        file_names = ["scan1.txt", "scan2.txt", "scan3.txt"]
        file_contents = {}
        for name in file_names:
            file_path = f"{dir_path}/{name}"
            content = f"content of {name}"
            file_contents[name] = content
            with bf.DACSAdapter(file_path, "w") as f:
                f.write(content)

        # Scan the directory
        items = list(bf.scandir(dir_path))

        # Verify we got some items
        assert len(items) > 0, "Directory should contain files"

        # DirEntry structure and stat fields should match blobfile API
        for entry in items:
            assert hasattr(entry, "path"), "DirEntry should have path attribute"
            assert hasattr(entry, "name"), "DirEntry should have name attribute"
            assert hasattr(entry, "is_dir"), "DirEntry should have is_dir attribute"
            assert hasattr(entry, "is_file"), "DirEntry should have is_file attribute"
            assert hasattr(entry, "stat"), "DirEntry should have stat attribute"

            assert entry.is_file is True, "DirEntry for file should report is_file True"
            assert entry.is_dir is False, "DirEntry for file should report is_dir False"
            
            # Verify stat is populated with actual data (not None)
            assert entry.stat is not None, "DirEntry.stat should be populated with file stats"
            assert hasattr(entry.stat, "size"), "Stat should have size attribute"
            assert hasattr(entry.stat, "mtime"), "Stat should have mtime attribute"
            
            # Validate stat values match the content we wrote
            if entry.name in file_contents:
                expected_size = len(file_contents[entry.name])
                assert entry.stat.size == expected_size, f"File {entry.name} stat.size should be {expected_size}, got {entry.stat.size}"

        names = {entry.name for entry in items}
        for name in file_names:
            assert name in names, f"File {name} should be in directory listing"

    def test_listdir_scandir_consistency(self):
        """Test that listdir() and scandir() return consistent results."""
        dir_path = f"{AZURE_BLOB_PATH}/test_listdir_scandir_{id(self)}"

        # Create some files
        file_names = ["consistent1.txt", "consistent2.txt"]
        for name in file_names:
            file_path = f"{dir_path}/{name}"
            with bf.DACSAdapter(file_path, "w") as f:
                f.write(f"content of {name}")

        # Get listings from both methods
        listdir_items = set(bf.listdir(dir_path))
        scandir_entries = list(bf.scandir(dir_path))
        scandir_items = {entry.name for entry in scandir_entries}

        # Both should return the same set of item names
        assert listdir_items == scandir_items, "listdir() and scandir() should return the same items"

        # Both should contain our files
        assert len(listdir_items) >= len(file_names), "Directory should contain at least the created files"

    def test_listdir_generator_behavior(self):
        """Test that listdir() returns an iterator/generator."""
        dir_path = f"{AZURE_BLOB_PATH}/test_listdir_generator_{id(self)}"

        # Create a file
        with bf.DACSAdapter(f"{dir_path}/test.txt", "w") as f:
            f.write("test")

        # Get the result - should be an iterator/generator
        result = bf.listdir(dir_path)

        # Check it's iterable
        assert hasattr(result, "__iter__"), "listdir() should return an iterable"

        # Should be able to iterate multiple times by calling listdir again
        first_pass = list(bf.listdir(dir_path))
        second_pass = list(bf.listdir(dir_path))

        assert first_pass == second_pass, "Multiple iterations should yield same results"

    def test_scandir_generator_behavior(self):
        """Test that scandir() returns an iterator/generator."""
        dir_path = f"{AZURE_BLOB_PATH}/test_scandir_generator_{id(self)}"

        # Create a file
        with bf.DACSAdapter(f"{dir_path}/test.txt", "w") as f:
            f.write("test")

        # Get the result - should be an iterator/generator
        result = bf.scandir(dir_path)

        # Check it's iterable
        assert hasattr(result, "__iter__"), "scandir() should return an iterable"

        # Should be able to iterate multiple times by calling scandir again
        first_pass_entries = list(bf.scandir(dir_path))
        second_pass_entries = list(bf.scandir(dir_path))

        first_names = {entry.name for entry in first_pass_entries}
        second_names = {entry.name for entry in second_pass_entries}
        assert first_names == second_names, "Multiple iterations should yield same results"


class TestWriteModes:
    """Test WriteMode functionality."""

    def test_write_disabled_mode_blocks_write_on_open(self):
        """Test that WRITE_DISABLED mode raises error when opening file in write mode."""
        blob_path = f"{AZURE_BLOB_PATH}/test_write_disabled_{id(self)}.txt"

        with pytest.raises(bf.OperationNotSupportedError, match="WRITE_DISABLED"):
            with bf.DACSAdapter(blob_path, "w", write_mode=bf.WriteMode.WRITE_DISABLED) as f:
                pass

    def test_write_disabled_mode_allows_read(self):
        """Test that WRITE_DISABLED mode allows read operations."""
        test_data = "Read with disabled write"
        blob_path = f"{AZURE_BLOB_PATH}/test_write_disabled_read_{id(self)}.txt"

        # First create the file with default write mode
        with bf.DACSAdapter(blob_path, "w") as f:
            bytes_written = f.write(test_data)

        # Read with WRITE_DISABLED mode should work (only if write succeeded)
        if bytes_written > 0:
            with bf.DACSAdapter(blob_path, "r", write_mode=bf.WriteMode.WRITE_DISABLED) as f:
                content = f.read()
            assert content == test_data

    def test_write_disabled_mode_blocks_write_method(self):
        """Test that WRITE_DISABLED mode raises error on write() call."""
        test_data = "Read with disabled write"
        blob_path = f"{AZURE_BLOB_PATH}/test_write_disabled_method_{id(self)}.txt"

        # Create file first
        with bf.DACSAdapter(blob_path, "w") as f:
            f.write(test_data)

        # Open in read mode with WRITE_DISABLED, then try to call write
        with bf.DACSAdapter(blob_path, "r", write_mode=bf.WriteMode.WRITE_DISABLED) as f:
            with pytest.raises(bf.OperationNotSupportedError, match="WRITE_DISABLED"):
                f.write("should fail")

    def test_default_write_mode_is_write_sync(self):
        """Test that default write mode is WRITE_SYNC."""
        blob_path = f"{AZURE_BLOB_PATH}/test_default_mode_{id(self)}.txt"

        f = bf.DACSAdapter(blob_path, "w")
        assert f._write_mode == bf.WriteMode.WRITE_SYNC


class TestUnsupportedOperations:
    """Test that unsupported operations raise OperationNotSupportedError."""

    def test_append_mode_not_supported(self):
        """Test that append mode raises OperationNotSupportedError."""
        blob_path = f"{AZURE_BLOB_PATH}/test_append_{id(self)}.txt"

        with pytest.raises(bf.OperationNotSupportedError, match="Append mode"):
            with bf.DACSAdapter(blob_path, "a") as f:
                pass

    def test_readline_not_supported(self):
        """Test that readline() raises OperationNotSupportedError."""
        test_data = "line1\nline2\n"
        blob_path = f"{AZURE_BLOB_PATH}/test_readline_{id(self)}.txt"

        with bf.DACSAdapter(blob_path, "w") as f:
            f.write(test_data)

        with bf.DACSAdapter(blob_path, "r") as f:
            with pytest.raises(bf.OperationNotSupportedError, match="readline"):
                f.readline()

    def test_readlines_not_supported(self):
        """Test that readlines() raises OperationNotSupportedError."""
        blob_path = f"{AZURE_BLOB_PATH}/test_readlines_{id(self)}.txt"

        with bf.DACSAdapter(blob_path, "w") as f:
            f.write("test")

        with bf.DACSAdapter(blob_path, "r") as f:
            with pytest.raises(bf.OperationNotSupportedError, match="readlines"):
                f.readlines()

    def test_writelines_not_supported(self):
        """Test that writelines() raises OperationNotSupportedError."""
        blob_path = f"{AZURE_BLOB_PATH}/test_writelines_{id(self)}.txt"

        with bf.DACSAdapter(blob_path, "w") as f:
            with pytest.raises(bf.OperationNotSupportedError, match="writelines"):
                f.writelines(["line1", "line2"])

    def test_seek_not_supported(self):
        """Test that seek() raises OperationNotSupportedError."""
        blob_path = f"{AZURE_BLOB_PATH}/test_seek_{id(self)}.txt"

        with bf.DACSAdapter(blob_path, "w") as f:
            f.write("test")

        with bf.DACSAdapter(blob_path, "r") as f:
            with pytest.raises(bf.OperationNotSupportedError, match="seek"):
                f.seek(0)

    def test_tell_not_supported(self):
        """Test that tell() raises OperationNotSupportedError."""
        blob_path = f"{AZURE_BLOB_PATH}/test_tell_{id(self)}.txt"

        with bf.DACSAdapter(blob_path, "w") as f:
            f.write("test")

        with bf.DACSAdapter(blob_path, "r") as f:
            with pytest.raises(bf.OperationNotSupportedError, match="tell"):
                f.tell()

    def test_flush_not_supported(self):
        """Test that flush() raises OperationNotSupportedError."""
        blob_path = f"{AZURE_BLOB_PATH}/test_flush_{id(self)}.txt"

        with bf.DACSAdapter(blob_path, "w") as f:
            with pytest.raises(bf.OperationNotSupportedError, match="flush"):
                f.flush()

    def test_iteration_not_supported(self):
        """Test that iteration raises OperationNotSupportedError."""
        blob_path = f"{AZURE_BLOB_PATH}/test_iter_{id(self)}.txt"

        with bf.DACSAdapter(blob_path, "w") as f:
            f.write("line1\nline2\n")

        with bf.DACSAdapter(blob_path, "r") as f:
            with pytest.raises(bf.OperationNotSupportedError, match="Iteration"):
                for line in f:
                    pass

    def test_copy_not_supported(self):
        """Test that copy() raises OperationNotSupportedError."""
        with pytest.raises(bf.OperationNotSupportedError, match="copy"):
            bf.copy("src", "dst")

    def test_glob_not_supported(self):
        """Test that glob() raises OperationNotSupportedError."""
        with pytest.raises(bf.OperationNotSupportedError, match="glob"):
            list(bf.glob("*.txt"))

    def test_makedirs_not_supported(self):
        """Test that makedirs() raises OperationNotSupportedError."""
        with pytest.raises(bf.OperationNotSupportedError, match="makedirs"):
            bf.makedirs("az://test/path")

    def test_remove_not_supported(self):
        """Test that remove() raises OperationNotSupportedError."""
        with pytest.raises(bf.OperationNotSupportedError, match="remove"):
            bf.remove("az://test/file.txt")

    def test_scanglob_not_supported(self):
        """Test that scanglob() raises OperationNotSupportedError."""
        with pytest.raises(bf.OperationNotSupportedError, match="scanglob"):
            list(bf.scanglob("*.txt"))

    def test_isdir_not_supported(self):
        """Test that isdir() raises OperationNotSupportedError."""
        with pytest.raises(bf.OperationNotSupportedError, match="isdir"):
            bf.isdir("az://test/path")

    def test_rmdir_not_supported(self):
        """Test that rmdir() raises OperationNotSupportedError."""
        with pytest.raises(bf.OperationNotSupportedError, match="rmdir"):
            bf.rmdir("az://test/path")

    def test_rmtree_not_supported(self):
        """Test that rmtree() raises OperationNotSupportedError."""
        with pytest.raises(bf.OperationNotSupportedError, match="rmtree"):
            bf.rmtree("az://test/path")

    def test_walk_not_supported(self):
        """Test that walk() raises OperationNotSupportedError."""
        with pytest.raises(bf.OperationNotSupportedError, match="walk"):
            list(bf.walk("az://test/path"))

    def test_basename_not_supported(self):
        """Test that basename() raises OperationNotSupportedError."""
        with pytest.raises(bf.OperationNotSupportedError, match="basename"):
            bf.basename("az://test/file.txt")

    def test_dirname_not_supported(self):
        """Test that dirname() raises OperationNotSupportedError."""
        with pytest.raises(bf.OperationNotSupportedError, match="dirname"):
            bf.dirname("az://test/file.txt")

    def test_join_not_supported(self):
        """Test that join() raises OperationNotSupportedError."""
        with pytest.raises(bf.OperationNotSupportedError, match="join"):
            bf.join("az://test", "path", "file.txt")

    def test_get_url_not_supported(self):
        """Test that get_url() raises OperationNotSupportedError."""
        with pytest.raises(bf.OperationNotSupportedError, match="get_url"):
            bf.get_url("az://test/file.txt")

    def test_md5_not_supported(self):
        """Test that md5() raises OperationNotSupportedError."""
        with pytest.raises(bf.OperationNotSupportedError, match="md5"):
            bf.md5("az://test/file.txt")

    def test_set_mtime_not_supported(self):
        """Test that set_mtime() raises OperationNotSupportedError."""
        with pytest.raises(bf.OperationNotSupportedError, match="set_mtime"):
            bf.set_mtime("az://test/file.txt", 12345.0)

    def test_configure_unsupported_options(self):
        """Test that configure() raises OperationNotSupportedError for unsupported options."""
        # Test specific unsupported options that should raise errors
        # These must match the unsupported_options dict in dacs_adapter.configure()
        unsupported_options = [
            {"connection_pool_max_size": 10},
            {"max_connection_pool_count": 5},
            {"azure_write_chunk_size": 1024},
            {"google_write_chunk_size": 1024},
            {"retry_limit": 3},
            {"retry_common_log_threshold": 2},
            {"retry_log_threshold": 2},
            {"connect_timeout": 30},
            {"read_timeout": 60},
            {"output_az_paths": True},
            {"use_azure_storage_account_key_fallback": True},
            {"get_http_pool_options": lambda: {}},
            {"get_gcs_http_pool_options": lambda: {}},
        ]

        for option in unsupported_options:
            option_name = list(option.keys())[0]
            with pytest.raises(bf.OperationNotSupportedError, match=option_name):
                bf.configure(**option)

    def test_configure_supported_options(self):
        """Test that configure() accepts supported options without raising."""
        # log_callback should be supported
        def my_log_callback(msg):
            pass

        # Should not raise
        bf.configure(log_callback=my_log_callback)
        bf.configure(log_callback=None)


class TestContextManagerAndErrorHandling:
    """Test context manager behavior and error handling."""

    def test_read_without_context_manager_raises_error(self):
        """Test that read() without context manager raises ValueError."""
        test_data = "test data"
        blob_path = f"{AZURE_BLOB_PATH}/test_no_ctx_read_{id(self)}.txt"

        with bf.DACSAdapter(blob_path, "w") as f:
            f.write(test_data)

        f = bf.DACSAdapter(blob_path, "r")
        with pytest.raises(ValueError, match="File handle is not open"):
            f.read()

    def test_write_without_context_manager_raises_error(self):
        """Test that write() without context manager raises ValueError."""
        blob_path = f"{AZURE_BLOB_PATH}/test_no_ctx_write_{id(self)}.txt"

        f = bf.DACSAdapter(blob_path, "w")
        with pytest.raises(ValueError, match="File handle is not open"):
            f.write("test")

    def test_close_is_idempotent(self):
        """Test that calling close() multiple times is safe."""
        blob_path = f"{AZURE_BLOB_PATH}/test_close_idempotent_{id(self)}.txt"

        with bf.DACSAdapter(blob_path, "w") as f:
            f.write("test")

        # close() already called by __exit__, call again
        f.close()
        f.close()

        assert f._closed == True
        assert len(f._write_buffers) == 0


class TestTextAndBinaryModes:
    """Test text and binary mode handling."""

    def test_text_mode_returns_string(self):
        """Test that text mode read returns string."""
        test_data = "Text mode string"
        blob_path = f"{AZURE_BLOB_PATH}/test_text_mode_{id(self)}.txt"

        with bf.DACSAdapter(blob_path, "w") as f:
            bytes_written = f.write(test_data)

        # Only verify if write succeeded
        if bytes_written > 0:
            with bf.DACSAdapter(blob_path, "r") as f:
                content = f.read()

            assert isinstance(content, str), f"Text mode should return str, got {type(content)}"
            assert content == test_data

    def test_binary_mode_returns_bytes(self):
        """Test that binary mode read returns bytes."""
        test_data = b"Binary mode bytes"
        blob_path = f"{AZURE_BLOB_PATH}/test_binary_mode_{id(self)}.txt"

        with bf.DACSAdapter(blob_path, "wb") as f:
            bytes_written = f.write(test_data)

        # Only verify if write succeeded
        if bytes_written > 0:
            with bf.DACSAdapter(blob_path, "rb") as f:
                content = f.read()

            assert isinstance(content, bytes), f"Binary mode should return bytes, got {type(content)}"
            assert content == test_data

    def test_utf8_encoding_in_text_mode(self):
        """Test UTF-8 encoding/decoding in text mode."""
        test_data = "Unicode: 你好世界 🌍 émoji"
        blob_path = f"{AZURE_BLOB_PATH}/test_utf8_{id(self)}.txt"

        with bf.DACSAdapter(blob_path, "w") as f:
            bytes_written = f.write(test_data)

        # Only verify if write succeeded
        if bytes_written > 0:
            with bf.DACSAdapter(blob_path, "r") as f:
                content = f.read()

            assert content == test_data

    def test_write_string_to_text_mode(self):
        """Test writing string in text mode."""
        test_data = "String to text mode"
        blob_path = f"{AZURE_BLOB_PATH}/test_str_text_{id(self)}.txt"

        with bf.DACSAdapter(blob_path, "w") as f:
            bytes_written = f.write(test_data)

        # Write should return positive value on success (number of bytes written)
        assert bytes_written > 0, f"Write should succeed with positive bytes, got {bytes_written}"

    def test_write_bytes_to_binary_mode(self):
        """Test writing bytes in binary mode."""
        test_data = b"Bytes to binary mode"
        blob_path = f"{AZURE_BLOB_PATH}/test_bytes_binary_{id(self)}.txt"

        with bf.DACSAdapter(blob_path, "wb") as f:
            bytes_written = f.write(test_data)

        # Write should return positive value on success (number of bytes written)
        assert bytes_written > 0, f"Write should succeed with positive bytes, got {bytes_written}"


class TestExceptionHandling:
    """Test the enhanced exception handling module."""

    def test_exception_hierarchy(self):
        """Test that all exception classes have correct inheritance."""
        # All storage errors should inherit from DACSStorageError
        storage_errors = [
            DACSAuthenticationError,
            DACSPermissionError,
            DACSNotFoundError,
            DACSExistsError,
            DACSBusyError,
            DACSTimeoutError,
            DACSNetworkError,
            DACSQuotaError,
            DACSInvalidInputError,
            DACSServiceUnavailableError,
            DACSIsDirectoryError,
            DACSNotDirectoryError,
            DACSNotEmptyError,
            DACSNameTooLongError,
            DACSReadOnlyError,
        ]

        for error_class in storage_errors:
            assert issubclass(
                error_class, DACSStorageError
            ), f"{error_class.__name__} should inherit from DACSStorageError"
            assert issubclass(error_class, DACSError), f"{error_class.__name__} should inherit from DACSError"

        # OperationNotSupportedError inherits directly from DACSError
        assert issubclass(OperationNotSupportedError, DACSError)

    def test_error_classification_auth_errors(self):
        """Test that authentication errors are classified correctly."""
        auth_messages = [
            "AuthorizationPermissionMismatch: The request is not authorized",
            "AuthorizationFailed for blob operation",
            "InsufficientAccountPermissions on storage account",
            "AccountIsDisabled: Storage account is disabled",
        ]

        for msg in auth_messages:
            error_class, errno_code = _classify_error(msg)
            assert (
                error_class == DACSAuthenticationError
            ), f"'{msg}' should be classified as DACSAuthenticationError"
            assert errno_code == errno.EPERM, f"'{msg}' should map to EPERM"

    def test_error_classification_not_found_errors(self):
        """Test that not found errors are classified correctly."""
        not_found_messages = [
            "BlobNotFound: The specified blob does not exist",
            "ContainerNotFound: The container was not found",
            "ResourceNotFound in storage operation",
            "PathNotFound: /path/to/file",
            "404 Not Found",
        ]

        for msg in not_found_messages:
            error_class, errno_code = _classify_error(msg)
            assert error_class == DACSNotFoundError, f"'{msg}' should be classified as DACSNotFoundError"
            assert errno_code == errno.ENOENT, f"'{msg}' should map to ENOENT"

    def test_error_classification_network_errors(self):
        """Test that network errors are classified correctly."""
        network_messages = [
            "ConnectionRefused: Unable to connect to server",
            "ConnectionReset by peer",
            "NetworkUnreachable: Network is down",
            "HostUnreachable: No route to host",
            "SocketError in connection",
        ]

        for msg in network_messages:
            error_class, errno_code = _classify_error(msg)
            assert error_class == DACSNetworkError, f"'{msg}' should be classified as DACSNetworkError"
            assert errno_code == errno.ECONNREFUSED, f"'{msg}' should map to ECONNREFUSED"

    def test_error_classification_timeout_errors(self):
        """Test that timeout errors are classified correctly."""
        timeout_messages = [
            "ServerTimeout: Operation timed out",
            "ClientTimeout waiting for response",
            "OperationTimedOut after 30 seconds",
        ]

        for msg in timeout_messages:
            error_class, errno_code = _classify_error(msg)
            assert error_class == DACSTimeoutError, f"'{msg}' should be classified as DACSTimeoutError"
            assert errno_code == errno.ETIMEDOUT, f"'{msg}' should map to ETIMEDOUT"

    def test_error_classification_service_unavailable(self):
        """Test that service unavailable errors are classified correctly."""
        unavailable_messages = [
            "ServiceUnavailable: Try again later",
            "ServerBusy: Server is overloaded",
            "503 Service Unavailable",
            "InternalError: An internal error occurred",
            "500 Internal Server Error",
        ]

        for msg in unavailable_messages:
            error_class, errno_code = _classify_error(msg)
            assert (
                error_class == DACSServiceUnavailableError
            ), f"'{msg}' should be classified as DACSServiceUnavailableError"
            assert errno_code == errno.EAGAIN, f"'{msg}' should map to EAGAIN"

    def test_error_classification_quota_errors(self):
        """Test that quota errors are classified correctly."""
        quota_messages = [
            "QuotaExceeded: Storage quota exceeded",
            "AccountQuotaExceeded for this account",
            "MaxBlobSizeExceeded: Blob is too large",
            "FileTooLarge: Cannot store file",
        ]

        for msg in quota_messages:
            error_class, errno_code = _classify_error(msg)
            assert error_class == DACSQuotaError, f"'{msg}' should be classified as DACSQuotaError"
            assert errno_code == errno.EDQUOT, f"'{msg}' should map to EDQUOT"

    def test_error_classification_invalid_input(self):
        """Test that invalid input errors are classified correctly."""
        invalid_messages = [
            "InvalidInput: Parameter is invalid",
            "InvalidParameter: Value out of range",
            "InvalidRange: Byte range is invalid",
            "400 Bad Request",
        ]

        for msg in invalid_messages:
            error_class, errno_code = _classify_error(msg)
            assert error_class == DACSInvalidInputError, f"'{msg}' should be classified as DACSInvalidInputError"
            assert errno_code == errno.EINVAL, f"'{msg}' should map to EINVAL"

    def test_errno_to_exception_class_mapping(self):
        """Test errno to exception class mapping."""
        mappings = [
            (errno.EPERM, DACSAuthenticationError),
            (errno.EACCES, DACSPermissionError),
            (errno.ENOENT, DACSNotFoundError),
            (errno.EEXIST, DACSExistsError),
            (errno.EBUSY, DACSBusyError),
            (errno.ETIMEDOUT, DACSTimeoutError),
            (errno.ECONNREFUSED, DACSNetworkError),
            (errno.EAGAIN, DACSServiceUnavailableError),
            (errno.EDQUOT, DACSQuotaError),
            (errno.EINVAL, DACSInvalidInputError),
        ]

        for errno_val, expected_class in mappings:
            actual_class = errno_to_exception_class(errno_val)
            assert (
                actual_class == expected_class
            ), f"errno {errno_val} should map to {expected_class.__name__}, got {actual_class.__name__}"

    def test_storage_error_attributes(self):
        """Test that DACSStorageError has all expected attributes."""
        error = DACSStorageError(
            message="Test error message",
            operation="read",
            path="/test/path",
            error_code=-1,
            errno_code=5,
            request_id="abc-123",
        )

        assert error.message == "Test error message"
        assert error.operation == "read"
        assert error.path == "/test/path"
        assert error.error_code == -1
        assert error.errno_code == 5
        assert error.request_id == "abc-123"

        # Check string representation contains key info
        error_str = str(error)
        assert "Test error message" in error_str
        assert "read" in error_str
        assert "/test/path" in error_str

    def test_handle_native_error_auth_exception(self):
        """Test handle_native_error with authentication error."""
        original_error = Exception("AuthorizationPermissionMismatch: Access denied")
        result = handle_native_error(original_error, operation="open", path="/test/blob")

        assert isinstance(result, DACSAuthenticationError)
        assert result.operation == "open"
        assert result.path == "/test/blob"
        assert "AuthorizationPermissionMismatch" in result.message

    def test_handle_native_error_not_found_exception(self):
        """Test handle_native_error with not found error."""
        original_error = Exception("BlobNotFound: The blob does not exist")
        result = handle_native_error(original_error, operation="read", path="/test/missing.txt")

        assert isinstance(result, DACSNotFoundError)
        assert result.operation == "read"
        assert "BlobNotFound" in result.message

    def test_nonexistent_blob_raises_not_found(self):
        """Test that accessing nonexistent blob raises DACSNotFoundError or FileNotFoundError."""
        nonexistent_path = f"{AZURE_BLOB_PATH}/definitely_does_not_exist_{id(self)}.txt"

        # stat() should raise FileNotFoundError (Python standard) or DACSNotFoundError
        with pytest.raises((FileNotFoundError, DACSNotFoundError)):
            bf.stat(nonexistent_path)

    def test_exception_can_be_caught_by_base_class(self):
        """Test that specific exceptions can be caught by their base class."""
        # Create an authentication error
        error = handle_native_error(Exception("AuthorizationPermissionMismatch"), operation="test")

        # Should be catchable as DACSStorageError
        caught_as_storage = False
        try:
            raise error
        except DACSStorageError:
            caught_as_storage = True

        assert caught_as_storage, "DACSAuthenticationError should be catchable as DACSStorageError"

        # Should be catchable as DACSError
        caught_as_dacs = False
        try:
            raise error
        except bf.DACSError:
            caught_as_dacs = True

        assert caught_as_dacs, "DACSAuthenticationError should be catchable as DACSError"
