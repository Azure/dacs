#!/usr/bin/env python3
# ---------------------------------------------------------------
# DACS Blobfile Patcher Tests
#
# ---------------------------------------------------------------

import os
import sys
import pytest

import blobfile as bf
from dacs_blobfile_patcher import (
    enable_dacs_backend,
    disable_dacs_backend,
    is_patched,
    DACS_SUPPORTED_APIS,
)
from py_dacs_client.dacs_adapter import DACSAdapter, OperationNotSupportedError

# Import the unique session path from conftest.py
from conftest import AZURE_BLOB_PATH


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
        expected = {'BlobFile', 'exists', 'listdir', 'scandir', 'stat'}
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
# Tests: Azure paths (az://) route to DACSAdapter,
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
            assert isinstance(f, DACSAdapter), "BlobFile write should return DACSAdapter"
            f.write(test_data)
        
        # Test BlobFile read
        with bf.BlobFile(test_path, "r") as f:
            assert isinstance(f, DACSAdapter), "BlobFile read should return DACSAdapter"
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

    def test_all_supported_apis_with_non_azure_path(self):
        """Test all supported APIs route to original blobfile for non-Azure paths."""
        import tempfile
        enable_dacs_backend()

        # Create a temporary directory and file for testing
        with tempfile.TemporaryDirectory() as temp_dir:
            test_path = os.path.join(temp_dir, f"patcher_test_{id(self)}.txt")
            test_data = "non-azure patcher routing test"

            # Test BlobFile write - should NOT return DACSAdapter
            with bf.BlobFile(test_path, "w") as f:
                assert not isinstance(f, DACSAdapter), "BlobFile write should NOT return DACSAdapter for non-Azure paths"
                f.write(test_data)

            # Test BlobFile read - should NOT return DACSAdapter
            with bf.BlobFile(test_path, "r") as f:
                assert not isinstance(f, DACSAdapter), "BlobFile read should NOT return DACSAdapter for non-Azure paths"
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


# =============================================================================
# CATEGORY 3: BLOCKING TESTS
# Tests: Unsupported APIs are blocked with OperationNotSupportedError,
# error messages are informative, and blocking only occurs when patched
# =============================================================================

class TestUnsupportedAPIsBlocked:
    """Test that unsupported APIs raise OperationNotSupportedError."""

    def test_copy_blocked_when_patched(self):
        """Test that copy() raises OperationNotSupportedError when patched."""
        enable_dacs_backend()
        with pytest.raises(OperationNotSupportedError, match="copy"):
            bf.copy("src", "dst")

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
            bf.copy("src", "dst")
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
            (bf.copy, ["src", "dst"]),
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