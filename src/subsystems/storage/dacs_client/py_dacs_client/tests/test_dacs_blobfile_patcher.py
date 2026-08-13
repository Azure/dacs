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
# DACS Blobfile Patcher Tests
#
# ---------------------------------------------------------------

import errno
import os
import sys
import pytest
from unittest.mock import patch, MagicMock

import blobfile as bf
from dacs_blobfile_patcher import (
    enable_dacs_backend,
    disable_dacs_backend,
    is_patched,
    DACS_SUPPORTED_APIS,
)
from py_dacs_client.dacs_blobfile_exception_handler import normalize_exception
from py_dacs_client.dacs_blobfile_adapter import DACSBlobfileAdapter
from py_dacs_client.dacs_exceptions import OperationNotSupportedError
from py_dacs_client.dacs_exceptions import (
    DACSNotFoundError,
    DACSPermissionError,
    DACSAuthenticationError,
    DACSIsDirectoryError,
    DACSNotDirectoryError,
    DACSExistsError,
    DACSTimeoutError,
    DACSBusyError,
    DACSQuotaError,
    DACSNetworkError,
    DACSInvalidInputError,
    DACSServiceUnavailableError,
    DACSNotEmptyError,
    DACSNameTooLongError,
    DACSReadOnlyError,
    DACSStorageError,
)

# Import the unique session path from conftest.py
from conftest import BASE_BLOB_PATH

# Use adapter-specific sub-path to avoid collisions with other adapter tests
AZURE_BLOB_PATH = f"{BASE_BLOB_PATH}/patcher"


@pytest.fixture(autouse=True)
def cleanup_patcher():
    """Ensure patcher is disabled before and after each test."""
    disable_dacs_backend()
    yield
    disable_dacs_backend()


# =============================================================================
# CATEGORY 1: LIFECYCLE AND FUNCTIONALITY TESTS
# Tests: enable/disable state management, auto-enable via environment variable,
# module restoration, and DACS_SUPPORTED_APIS configuration validation
# =============================================================================

class TestPatcherLifecycle:
    """Test enable/disable/is_patched lifecycle."""

    def test_initial_state_is_unpatched(self):
        """Test that patcher starts in unpatched state."""
        assert is_patched() is False

    def test_enable_sets_patched_state(self):
        """Test that enable_dacs_backend() sets patched state."""
        enable_dacs_backend()
        assert is_patched() is True

    def test_disable_clears_patched_state(self):
        """Test that disable_dacs_backend() clears patched state."""
        enable_dacs_backend()
        assert is_patched() is True
        disable_dacs_backend()
        assert is_patched() is False

    def test_enable_is_idempotent(self):
        """Test that calling enable_dacs_backend() multiple times is safe."""
        enable_dacs_backend()
        enable_dacs_backend()
        enable_dacs_backend()
        assert is_patched() is True

    def test_disable_is_idempotent(self):
        """Test that calling disable_dacs_backend() multiple times is safe."""
        disable_dacs_backend()
        disable_dacs_backend()
        assert is_patched() is False

    def test_disable_without_enable_is_safe(self):
        """Test that disabling without enabling is safe."""
        disable_dacs_backend()
        assert is_patched() is False

    def test_enable_disable_cycle(self):
        """Test multiple enable/disable cycles work correctly."""
        for _ in range(3):
            enable_dacs_backend()
            assert is_patched() is True
            disable_dacs_backend()
            assert is_patched() is False


class TestOriginalBehaviorRestored:
    """Test that disable restores original blobfile behavior."""

    def test_module_class_restored_after_disable(self):
        """Test that module class is restored after disable."""
        original_class = bf.__class__
        
        enable_dacs_backend()
        
        # After patching, class should be different
        assert bf.__class__ is not original_class
        
        disable_dacs_backend()
        
        # After disabling, class should be restored
        assert bf.__class__ is original_class


class TestEnvironmentVariable:
    """Test BLOBFILE_USE_DACS environment variable behavior."""

    def test_supported_apis_constant_matches_patched_functions(self):
        """Test that DACS_SUPPORTED_APIS matches actual patched functions."""
        # Verify the constant contains expected APIs
        expected = {'BlobFile', 'copy', 'exists', 'isdir', 'join', 'listdir', 'scandir', 'stat'}
        assert DACS_SUPPORTED_APIS == expected

    def test_auto_enable_via_env_var(self):
        """Test auto-enable via BLOBFILE_USE_DACS=1 environment variable."""
        import importlib
        import dacs_blobfile_patcher
        
        # First check it's disabled
        assert is_patched() is False
        
        # Set env var and reload module to trigger auto-enable on import
        os.environ['BLOBFILE_USE_DACS'] = '1'
        importlib.reload(dacs_blobfile_patcher)
        
        # Now it should be enabled
        assert is_patched() is True
        
        # Cleanup
        os.environ.pop('BLOBFILE_USE_DACS', None)
        disable_dacs_backend()


# =============================================================================
# CATEGORY 2: ROUTING TESTS
# Tests: Azure paths (az://) route to DACSBlobfileAdapter,
# non-Azure paths route to original blobfile implementation
# =============================================================================

class TestPatcherRouting:
    """Test that routing works: az:// -> DACS, non-az -> original."""

    def test_all_supported_apis_with_azure_path(self):
        """Test all supported APIs route correctly to DACS with real Azure path."""
        enable_dacs_backend()
        
        # Create a test file using patched bf.BlobFile
        test_path = f"{AZURE_BLOB_PATH}/patcher_test_{id(self)}.txt"
        test_data = "patcher routing test"
        
        # Test BlobFile write
        with bf.BlobFile(test_path, "w") as f:
            assert hasattr(f, '_adapter') or isinstance(f, DACSBlobfileAdapter), "BlobFile write should use DACSBlobfileAdapter"
            f.write(test_data)
        
        # Test BlobFile read
        with bf.BlobFile(test_path, "r") as f:
            assert hasattr(f, '_adapter') or isinstance(f, DACSBlobfileAdapter), "BlobFile read should use DACSBlobfileAdapter"
            content = f.read()
            assert content == test_data, "Content should match written data"
        
        # Test exists
        result = bf.exists(test_path)
        assert result is True, "exists() should return True for existing file"
        
        stat_result = bf.stat(test_path)
        assert hasattr(stat_result, "size"), "stat() result should have size attribute"
        assert hasattr(stat_result, "mtime"), "stat() result should have mtime attribute"
        assert hasattr(stat_result, "ctime"), "stat() result should have ctime attribute"
        assert hasattr(stat_result, "md5"), "stat() result should have md5 attribute"
        assert hasattr(stat_result, "version"), "stat() result should have version attribute"
        assert hasattr(stat_result, "st_mode"), "stat() result should have st_mode attribute"
        # For Azure, md5 and version should be None
        assert stat_result.md5 is None, "md5 should be None for Azure paths"
        assert stat_result.version is None, "version should be None for Azure paths"
        assert stat_result.size == len(test_data), "stat() should return correct size"
        
        # Test listdir - list the parent directory
        parent_dir = "/".join(test_path.rsplit("/", 1)[:-1]) + "/"
        files = list(bf.listdir(parent_dir))
        assert any(f"patcher_test_{id(self)}.txt" in f for f in files), "listdir() should find our test file"
        
        # Test scandir - should return DirEntry objects
        entries = list(bf.scandir(parent_dir))
        assert len(entries) > 0, "scandir() should return entries"
        # Check that entries have DirEntry structure
        for entry in entries:
            assert hasattr(entry, "path"), "DirEntry should have path attribute"
            assert hasattr(entry, "name"), "DirEntry should have name attribute"
            assert hasattr(entry, "is_dir"), "DirEntry should have is_dir attribute"
            assert hasattr(entry, "is_file"), "DirEntry should have is_file attribute"
            assert hasattr(entry, "stat"), "DirEntry should have stat attribute"

        # Test copy - remote (az://) to local
        import tempfile
        with tempfile.TemporaryDirectory() as temp_dir:
            local_copy_path = os.path.join(temp_dir, "copied_file.txt")
            bf.copy(test_path, local_copy_path)
            assert os.path.exists(local_copy_path), "copy() should create local file"
            with open(local_copy_path, "r") as f:
                assert f.read() == test_data, "copy() should preserve content"

    def test_all_supported_apis_with_non_azure_path(self):
        """Test all supported APIs route to original blobfile for non-Azure paths."""
        import tempfile
        enable_dacs_backend()

        # Create a temporary directory and file for testing
        with tempfile.TemporaryDirectory() as temp_dir:
            test_path = os.path.join(temp_dir, f"patcher_test_{id(self)}.txt")
            test_data = "non-azure patcher routing test"

            # Test BlobFile write - should NOT return DACSBlobfileAdapter
            with bf.BlobFile(test_path, "w") as f:
                assert not isinstance(f, DACSBlobfileAdapter), "BlobFile write should NOT return DACSBlobfileAdapter for non-Azure paths"
                f.write(test_data)

            # Test BlobFile read - should NOT return DACSBlobfileAdapter
            with bf.BlobFile(test_path, "r") as f:
                assert not isinstance(f, DACSBlobfileAdapter), "BlobFile read should NOT return DACSBlobfileAdapter for non-Azure paths"
                content = f.read()
                assert content == test_data, "Content should match written data"

            # Test exists - should use original blobfile
            result = bf.exists(test_path)
            assert result is True, "exists() should return True for existing file"

            # Test stat - should use original blobfile
            stat_result = bf.stat(test_path)
            assert hasattr(stat_result, "size"), "stat() result should have size attribute"
            assert hasattr(stat_result, "mtime"), "stat() result should have mtime attribute"
            assert hasattr(stat_result, "ctime"), "stat() result should have ctime attribute"
            assert hasattr(stat_result, "md5"), "stat() result should have md5 attribute"
            assert hasattr(stat_result, "version"), "stat() result should have version attribute"
            assert not hasattr(stat_result, "st_mode"), "stat() result should NOT have st_mode attribute for non-Azure paths"
            assert stat_result.size == len(test_data), "stat() should return correct size for non-Azure paths"

            # Test listdir - should use original blobfile
            files = list(bf.listdir(temp_dir))
            assert f"patcher_test_{id(self)}.txt" in files, "listdir() should find our test file"

            # Test scandir - should return DirEntry objects
            entries = list(bf.scandir(temp_dir))
            assert len(entries) > 0, "scandir() should return entries"
            # Check that entries have DirEntry structure
            for entry in entries:
                assert hasattr(entry, "path"), "DirEntry should have path attribute"
                assert hasattr(entry, "name"), "DirEntry should have name attribute"
                assert hasattr(entry, "is_dir"), "DirEntry should have is_dir attribute"
                assert hasattr(entry, "is_file"), "DirEntry should have is_file attribute"
                assert hasattr(entry, "stat"), "DirEntry should have stat attribute"

            # Test copy - local to local (should use original blobfile)
            local_copy_path = os.path.join(temp_dir, "copied_file.txt")
            bf.copy(test_path, local_copy_path)
            assert os.path.exists(local_copy_path), "copy() should create local file"
            with open(local_copy_path, "r") as f:
                assert f.read() == test_data, "copy() should preserve content"


# =============================================================================
# CATEGORY 3: BLOCKING TESTS
# Tests: Unsupported APIs are blocked with OperationNotSupportedError,
# error messages are informative, and blocking only occurs when patched
# =============================================================================

class TestUnsupportedAPIsBlocked:
    """Test that unsupported APIs raise OperationNotSupportedError."""

    def test_remove_blocked_when_patched(self):
        """Test that remove() raises OperationNotSupportedError when patched."""
        enable_dacs_backend()
        with pytest.raises(OperationNotSupportedError, match="remove"):
            bf.remove("path")

    def test_glob_blocked_when_patched(self):
        """Test that glob() raises OperationNotSupportedError when patched."""
        enable_dacs_backend()
        with pytest.raises(OperationNotSupportedError, match="glob"):
            list(bf.glob("*.txt"))

    def test_makedirs_blocked_when_patched(self):
        """Test that makedirs() raises OperationNotSupportedError when patched."""
        enable_dacs_backend()
        with pytest.raises(OperationNotSupportedError, match="makedirs"):
            bf.makedirs("/some/path")

    def test_rmtree_blocked_when_patched(self):
        """Test that rmtree() raises OperationNotSupportedError when patched."""
        enable_dacs_backend()
        with pytest.raises(OperationNotSupportedError, match="rmtree"):
            bf.rmtree("/some/path")

    def test_error_message_lists_supported_apis(self):
        """Test that error message includes list of supported APIs."""
        enable_dacs_backend()
        try:
            bf.remove("path")
            assert False, "Should have raised OperationNotSupportedError"
        except OperationNotSupportedError as e:
            error_msg = str(e)
            # Verify all supported APIs are mentioned
            for api in DACS_SUPPORTED_APIS:
                assert api in error_msg, f"Error message should mention {api}"

    def test_unsupported_apis_work_when_unpatched(self):
        """Test that unsupported APIs work normally when patcher is disabled."""
        disable_dacs_backend()
        
        # These should NOT raise OperationNotSupportedError when unpatched
        # They may fail for other reasons, but not with our custom error
        unsupported_apis = [
            (bf.remove, ["path"]),
            (bf.glob, ["*.txt"]),
            (bf.makedirs, ["/some/path"]),
            (bf.rmtree, ["/some/path"]),
        ]
        
        for api_func, args in unsupported_apis:
            try:
                if api_func == bf.glob:
                    list(api_func(*args))  # glob returns a generator
                else:
                    api_func(*args)
            except OperationNotSupportedError:
                pytest.fail(f"{api_func.__name__}() should not raise OperationNotSupportedError when unpatched")
            except Exception:
                pass  # Other exceptions are fine (file not found, etc)


# =============================================================================
# CATEGORY 4: ALLOW THROUGH TESTS
# Tests: Dunder attributes (__name__, __file__), private attributes (_*),
# and non-callable attributes (constants) pass through without blocking
# =============================================================================

class TestAttributePassthrough:
    """Test that dunder, private, and non-callable attributes pass through."""

    def test_dunder_attributes_accessible_when_patched(self):
        """Test that __name__, __file__, etc. are accessible when patched."""
        enable_dacs_backend()
        
        # These should not raise OperationNotSupportedError
        _ = bf.__name__
        _ = bf.__file__
        _ = bf.__doc__

    def test_private_attributes_accessible_when_patched(self):
        """Test that _private attributes are accessible when patched."""
        enable_dacs_backend()
        
        # Access any _private attribute should not raise
        # (may raise AttributeError if doesn't exist, but not OperationNotSupportedError)
        try:
            _ = getattr(bf, '_some_private', None)
        except OperationNotSupportedError:
            pytest.fail("Private attributes should not be blocked")


class TestNonCallableAttributesPassthrough:
    """Test that non-callable attributes are not blocked."""

    def test_constants_accessible_when_patched(self):
        """Test that module constants are accessible when patched."""
        enable_dacs_backend()
        
        # Try accessing any non-callable attribute
        # Should not raise OperationNotSupportedError
        try:
            # Check for common blobfile attributes
            if hasattr(bf, 'AZ'):
                _ = bf.AZ
            if hasattr(bf, 'AZURE'):
                _ = bf.AZURE
        except OperationNotSupportedError:
            pytest.fail("Non-callable attributes should not be blocked")


# =============================================================================
# CATEGORY 5: EXCEPTION CONVERSION TESTS
# Tests: DACSErrors are converted to standard Python exceptions with
# proper errno, strerror, and filename attributes. Standard exceptions
# pass through unchanged.
# =============================================================================

class TestExceptionConversion:
    """Test that DACSErrors are converted to standard Python exceptions."""

    @pytest.mark.parametrize("dacs_exc_class,expected_builtin_class,errno_code,message,test_path", [
        (DACSNotFoundError, FileNotFoundError, errno.ENOENT, "File not found", "az://account/container/missing.txt"),
        (DACSPermissionError, PermissionError, errno.EACCES, "Permission denied", "az://account/container/restricted.txt"),
        (DACSAuthenticationError, PermissionError, errno.EPERM, "Authentication failed", "az://account/container/auth.txt"),
        (DACSIsDirectoryError, IsADirectoryError, errno.EISDIR, "Is a directory", "az://account/container/somedir/"),
        (DACSNotDirectoryError, NotADirectoryError, errno.ENOTDIR, "Not a directory", "az://account/container/notdir"),
        (DACSExistsError, FileExistsError, errno.EEXIST, "File exists", "az://account/container/existing.txt"),
        (DACSTimeoutError, TimeoutError, errno.ETIMEDOUT, "Operation timed out", "az://account/container/slow.txt"),
        (DACSNetworkError, OSError, errno.ECONNREFUSED, "Connection refused", "az://account/container/file.txt"),
        (DACSNetworkError, OSError, errno.ENETUNREACH, "Network unreachable", "az://account/container/file.txt"),
        (DACSNetworkError, OSError, errno.EHOSTUNREACH, "Host unreachable", "az://account/container/file.txt"),
        (DACSNetworkError, OSError, errno.ECONNRESET, "Connection reset", "az://account/container/file.txt"),
        (DACSBusyError, OSError, errno.EBUSY, "Resource busy", "az://account/container/busy.txt"),
        (DACSQuotaError, OSError, errno.EDQUOT, "Quota exceeded", "az://account/container/file.txt"),
        (DACSQuotaError, OSError, errno.EFBIG, "File too large", "az://account/container/big.txt"),
        (DACSInvalidInputError, OSError, errno.EINVAL, "Invalid input", "az://account/container/file.txt"),
        (DACSServiceUnavailableError, OSError, errno.EAGAIN, "Service unavailable", "az://account/container/file.txt"),
        (DACSNotEmptyError, OSError, errno.ENOTEMPTY, "Directory not empty", "az://account/container/dir/"),
        (DACSNameTooLongError, OSError, errno.ENAMETOOLONG, "Name too long", "az://account/container/verylongname.txt"),
        (DACSReadOnlyError, PermissionError, errno.EROFS, "Read-only filesystem", "az://account/container/file.txt"),
        (DACSStorageError, OSError, errno.EIO, "I/O error", "az://account/container/file.txt"),
    ])
    def test_dacs_exception_conversion(self, dacs_exc_class, expected_builtin_class, errno_code, message, test_path):
        """Test that DACSErrors are converted to the appropriate standard Python exceptions."""
        exc = dacs_exc_class(
            message=message,
            path=test_path,
            errno_code=errno_code
        )
        
        converted = normalize_exception(exc)
        
        assert isinstance(converted, expected_builtin_class)
        assert converted.errno == errno_code
        assert converted.strerror == message
        assert converted.filename == test_path
        # Verify diagnostic attributes are preserved
        assert hasattr(converted, 'errno_code')
        assert converted.errno_code == errno_code

    def test_standard_exceptions_pass_through_unchanged(self):
        """Test that standard Python exceptions are not converted."""
        test_exceptions = [
            ValueError("test value error"),
            TypeError("test type error"),
            AttributeError("test attribute error"),
            FileNotFoundError(errno.ENOENT, os.strerror(errno.ENOENT), "test.txt"),
            PermissionError(errno.EACCES, os.strerror(errno.EACCES), "test.txt"),
        ]
        
        for exc in test_exceptions:
            converted = normalize_exception(exc)
            assert converted is exc, f"{type(exc).__name__} should pass through unchanged"

    def test_conversion_preserves_diagnostic_attributes(self):
        """Test that conversion preserves all diagnostic attributes from DACSError."""
        exc = DACSNotFoundError(
            message="File not found",
            path="az://account/container/file.txt",
            errno_code=errno.ENOENT,
            error_code="BlobNotFound",
            request_id="req-12345"
        )
        
        converted = normalize_exception(exc)
        
        # Verify diagnostic attributes are preserved
        assert hasattr(converted, 'path')
        assert converted.path == "az://account/container/file.txt"
        assert hasattr(converted, 'errno_code')
        assert converted.errno_code == errno.ENOENT
        assert hasattr(converted, 'error_code')
        assert converted.error_code == "BlobNotFound"
        assert hasattr(converted, 'request_id')
        assert converted.request_id == "req-12345"

    def test_patched_functions_raise_converted_exceptions(self):
        """Test that patched functions properly convert and raise exceptions."""
        enable_dacs_backend()
        
        # Use a non-existent Azure path
        nonexistent_path = f"{AZURE_BLOB_PATH}/nonexistent_file_{id(self)}.txt"
        
        # Test exists - should return False, not raise
        result = bf.exists(nonexistent_path)
        assert result is False
        
        # Test stat - should raise FileNotFoundError
        with pytest.raises(FileNotFoundError) as exc_info:
            bf.stat(nonexistent_path)
        
        # Verify the exception has proper attributes
        assert exc_info.value.errno == errno.ENOENT
        assert exc_info.value.filename == nonexistent_path
        assert exc_info.value.strerror  # Original message is preserved as strerror

    def test_patched_blobfile_raises_converted_exceptions(self):
        """Test that BlobFile properly converts exceptions on file operations."""
        enable_dacs_backend()
        
        # Try to read a non-existent file
        nonexistent_path = f"{AZURE_BLOB_PATH}/nonexistent_file_{id(self)}.txt"
        
        with pytest.raises(FileNotFoundError) as exc_info:
            with bf.BlobFile(nonexistent_path, "r") as f:
                f.read()
        
        # Verify the exception has proper attributes
        assert exc_info.value.errno == errno.ENOENT
        assert exc_info.value.strerror  # Original message is preserved as strerror

    def test_patched_copy_nonexistent_source_raises_converted_exception(self):
        """Test that copy with non-existent source raises FileNotFoundError, not DACSError."""
        enable_dacs_backend()

        nonexistent_src = f"{AZURE_BLOB_PATH}/nonexistent_copy_src_{id(self)}.txt"
        dst_path = "/tmp/test_copy_dst_patcher.txt"

        with pytest.raises(FileNotFoundError) as exc_info:
            bf.copy(nonexistent_src, dst_path)

        assert exc_info.value.errno == errno.ENOENT

    def test_bf_error_is_oserror_when_patched(self):
        """Test that bf.Error is set to OSError when patcher is enabled."""
        enable_dacs_backend()
        assert bf.Error is OSError

    def test_bf_error_restored_when_unpatched(self):
        """Test that bf.Error is restored to original when patcher is disabled."""
        original_error = bf.Error
        enable_dacs_backend()
        assert bf.Error is OSError
        disable_dacs_backend()
        assert bf.Error is original_error

    def test_bf_error_catches_converted_exceptions(self):
        """Test that except bf.Error catches our converted OSError subclasses."""
        enable_dacs_backend()

        nonexistent_path = f"{AZURE_BLOB_PATH}/nonexistent_bf_error_{id(self)}.txt"

        # bf.Error (= OSError) should catch FileNotFoundError since it's a subclass
        with pytest.raises(bf.Error):
            bf.stat(nonexistent_path)


class TestJoinRouting:
    """Test that bf.join works correctly when patcher is enabled."""

    def test_join_azure_and_local(self):
        """Test join() with az:// paths, '..' resolution, absolute resets, and local paths."""
        enable_dacs_backend()

        # Multi-component az:// join
        assert bf.join("az://account/container", "path", "file.txt") == "az://account/container/path/file.txt"

        # Simple az:// join
        assert bf.join("az://account/container", "file.txt") == "az://account/container/file.txt"

        # '..' resolves within blob path
        assert bf.join("az://account/container/a/b", "..", "c.txt") == "az://account/container/a/c.txt"

        # Absolute component resets blob path
        assert bf.join("az://account/container/old", "/new/file.txt") == "az://account/container/new/file.txt"

        # Local paths
        expected_local = os.path.join("/home/user", "dir", "file.txt")
        assert bf.join("/home/user", "dir", "file.txt") == expected_local