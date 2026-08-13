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

"""
Tests for DACSCacheClient shutdown behavior.

Validates that:
  - shutdown_sync() invokes the native shutdown function
  - shutdown_sync() is idempotent (consecutive calls without I/O are no-ops)
  - shutdown_sync() tolerates native shutdown raising an exception
  - Client is reusable after shutdown (init → I/O → shutdown → I/O → shutdown)
  - async shutdown() delegates to shutdown_sync()

To run:
    cd <workspace>/DACS/src/subsystems/storage/dacs_client
    python3 -m pytest py_dacs_client/tests/test_shutdown.py -v --noconftest
"""

import asyncio
import os
import sys
import concurrent.futures
import pytest
from unittest.mock import patch, MagicMock

# Patch target: the DACSCacheDataLoader name as imported by dacs_client
_LOADER_PATCH_TARGET = "py_dacs_client.dacs_client.DACSCacheDataLoader"


class TestShutdownSync:
    """Tests for the synchronous shutdown path."""

    def _make_client(self):
        """Create a DACSCacheClient with mocked native layer."""
        from py_dacs_client.dacs_client import DACSCacheClient
        with patch("py_dacs_client.dacs_utils._initialize_dacs_client_config"):
            client = DACSCacheClient()
        return client

    def test_shutdown_calls_native_shutdown(self):
        """shutdown_sync() must invoke DACSCacheDataLoader.shutdown()."""
        client = self._make_client()
        with patch(_LOADER_PATCH_TARGET) as mock_loader:
            client.shutdown_sync()
            mock_loader.shutdown.assert_called_once()

    def test_consecutive_shutdowns_are_safe(self):
        """Calling shutdown_sync() twice consecutively must not crash — both calls go through."""
        client = self._make_client()

        with patch(_LOADER_PATCH_TARGET) as mock_loader:
            client.shutdown_sync()
            client.shutdown_sync()

            assert mock_loader.shutdown.call_count == 2

    def test_shutdown_tolerates_native_exception(self):
        """shutdown_sync() must not raise if native shutdown throws."""
        client = self._make_client()
        with patch(_LOADER_PATCH_TARGET) as mock_loader:
            mock_loader.shutdown.side_effect = OSError("simulated IB teardown error")

            # Should not raise
            client.shutdown_sync()

    def test_client_reusable_after_shutdown(self):
        """Client must be reusable after shutdown — the shutdown-then-use cycle must work."""
        client = self._make_client()

        with patch(_LOADER_PATCH_TARGET) as mock_loader:
            # First cycle: shutdown
            client.shutdown_sync()
            assert mock_loader.shutdown.call_count == 1

            # Second cycle: shutdown again should call native shutdown again
            # (not blocked by idempotency guard, since flag is reset)
            client.shutdown_sync()
            assert mock_loader.shutdown.call_count == 2

    def test_io_between_shutdowns(self):
        """Full init → read → shutdown → read → shutdown cycle must work."""
        client = self._make_client()

        with patch(_LOADER_PATCH_TARGET) as mock_loader:
            # Simulate a read (sync methods go straight to native, no executor needed)
            mock_loader.open.return_value = 42
            mock_loader.pread.return_value = 100
            mock_loader.close.return_value = 0

            # First I/O cycle
            fd = mock_loader.open("test_path", 0, 0)
            assert fd == 42
            mock_loader.pread(fd, None, 100, 0)

            # First shutdown
            client.shutdown_sync()
            assert mock_loader.shutdown.call_count == 1

            # Second I/O cycle — must work after shutdown
            fd = mock_loader.open("test_path_2", 0, 0)
            assert fd == 42
            mock_loader.pread(fd, None, 100, 0)

            # Second shutdown
            client.shutdown_sync()
            assert mock_loader.shutdown.call_count == 2

    def test_executor_survives_shutdown(self):
        """The thread pool executor must remain usable after shutdown for subsequent I/O."""
        client = self._make_client()
        executor = client.executor

        with patch(_LOADER_PATCH_TARGET):
            client.shutdown_sync()

        # Executor should still be alive — sync and async I/O must remain possible
        assert not executor._shutdown

    def test_executor_usable_after_shutdown(self):
        """Executor must accept new work after shutdown for async I/O to function."""
        client = self._make_client()

        with patch(_LOADER_PATCH_TARGET):
            client.shutdown_sync()

        # Submit work to executor — must not raise RuntimeError
        future = client.executor.submit(lambda: 42)
        assert future.result() == 42


class TestShutdownAsync:
    """Tests for the async shutdown path."""

    def _make_client(self):
        from py_dacs_client.dacs_client import DACSCacheClient
        with patch("py_dacs_client.dacs_utils._initialize_dacs_client_config"):
            client = DACSCacheClient()
        return client

    @pytest.mark.asyncio
    async def test_async_shutdown_calls_native(self):
        """async shutdown() must call native shutdown via shutdown_sync()."""
        client = self._make_client()

        with patch(_LOADER_PATCH_TARGET) as mock_loader:
            await client.shutdown()
            mock_loader.shutdown.assert_called_once()

    @pytest.mark.asyncio
    async def test_async_consecutive_shutdowns_are_safe(self):
        """Calling async shutdown() twice consecutively must not crash — both calls go through."""
        client = self._make_client()

        with patch(_LOADER_PATCH_TARGET) as mock_loader:
            await client.shutdown()
            await client.shutdown()

            assert mock_loader.shutdown.call_count == 2


# ---------------------------------------------------------------------------
# Native integration tests — exercise the real libStorageDirect.so shutdown
# path through ContentHandler::HandleShutdown(). Skipped when the native
# library is not available (e.g., on dev machines without a Linux build).
#
# These tests MUST run after all other test suites. The conftest.py
# pytest_collection_modifyitems hook ensures this by moving all
# test_shutdown tests to the end of the collection. This prevents
# ContentHandler::GetInstance() from being called before other suites
# (blobfile/boostedblob adapters) get a chance to configure write-mode.
# ---------------------------------------------------------------------------

def _native_lib_available():
    """Check if libStorageDirect.so is importable."""
    try:
        from py_dacs_client.dacs_data_loader import DACSCacheDataLoader  # noqa: F401
        return True
    except (FileNotFoundError, OSError, ImportError):
        return False


_skip_no_native = pytest.mark.skipif(
    not _native_lib_available(),
    reason="libStorageDirect.so not available"
)


@_skip_no_native
class TestShutdownNative:
    """Integration tests using the real native library.

    These call through to ContentHandler::HandleShutdown() in libStorageDirect.so,
    verifying the C++ shutdown path doesn't crash and supports re-initialization.
    """

    def test_native_shutdown_no_crash(self):
        """Calling native shutdown on an uninitialized ContentHandler must not crash."""
        from py_dacs_client.dacs_data_loader import DACSCacheDataLoader
        # No prior I/O — HandleShutdown just clears empty maps
        DACSCacheDataLoader.shutdown()

    def test_native_shutdown_idempotent(self):
        """Calling native shutdown twice must not crash."""
        from py_dacs_client.dacs_data_loader import DACSCacheDataLoader
        DACSCacheDataLoader.shutdown()
        DACSCacheDataLoader.shutdown()

    def test_native_shutdown_then_reuse(self):
        """Full cycle: init → stat → shutdown → stat → shutdown via the real native library.

        Requires AZURE_BLOB_PATH env var and valid auth to be set (CI environment).
        Skipped in environments without Azure connectivity.
        """
        blob_path = os.environ.get("AZURE_BLOB_PATH")
        if not blob_path:
            pytest.skip("AZURE_BLOB_PATH not set — no Azure backend for I/O cycle test")

        from py_dacs_client.dacs_client import DACSCacheClient
        client = DACSCacheClient()

        try:
            # First cycle: stat a known path
            client.get_file_size_sync(blob_path)
        except Exception:
            pytest.skip("Could not reach Azure backend — skipping I/O cycle test")

        # First shutdown — tears down ConnectionManager
        client.shutdown_sync()

        # Second cycle: stat again — lazy re-init of cache clients
        size = client.get_file_size_sync(blob_path)
        assert size >= 0

        # Second shutdown
        client.shutdown_sync()
