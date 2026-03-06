#!/usr/bin/env python3
"""
Tests for BoundedBufferManager memory limiting behavior.

Validates that:
  - acquire_buffer blocks when the memory limit is reached
  - current_memory_usage is not corrupted when an acquire is denied
  - the back-pressure mechanism (buffer_released_event) actually works
"""

import asyncio
import os
import sys
import pytest
from unittest.mock import patch

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from dacs_utils import BoundedBufferManager


class FakeBuffer:
    """Lightweight stand-in for the ctypes buffer returned by alloc_buffer."""

    def __init__(self, size):
        self._size = size

    def __len__(self):
        return self._size

    def __bool__(self):
        return True


def _fake_alloc(size):
    return FakeBuffer(size)


def _fake_release(buf):
    pass


@pytest.mark.asyncio
class TestBoundedBufferManagerMemoryLimiting:

    @patch("dacs_utils.release_buffer", side_effect=_fake_release)
    @patch("dacs_utils.alloc_buffer", side_effect=_fake_alloc)
    async def test_acquire_blocks_when_memory_exhausted(self, mock_alloc, _mock_rel):
        """When memory limit is reached, acquire_buffer must block — not return."""
        manager = BoundedBufferManager(max_memory_limit=100)

        # First acquire: fills the limit exactly.
        buf1 = await manager.acquire_buffer(100)
        assert buf1 is not None
        assert manager.current_memory_usage == 100

        # Second acquire should block because there is no room for 50 more bytes.
        with pytest.raises(asyncio.TimeoutError):
            await asyncio.wait_for(manager.acquire_buffer(50), timeout=1.0)

    @patch("dacs_utils.release_buffer", side_effect=_fake_release)
    @patch("dacs_utils.alloc_buffer", side_effect=_fake_alloc)
    async def test_memory_usage_not_corrupted_when_limit_reached(self, mock_alloc, _mock_rel):
        """current_memory_usage must stay accurate when an acquire cannot proceed."""
        manager = BoundedBufferManager(max_memory_limit=100)

        await manager.acquire_buffer(100)
        assert manager.current_memory_usage == 100

        # Attempt an acquire that cannot be satisfied — then cancel it.
        task = asyncio.create_task(manager.acquire_buffer(50))
        await asyncio.sleep(0.15)
        task.cancel()
        try:
            await asyncio.wait_for(task, timeout=1.0)
        except (asyncio.CancelledError, asyncio.TimeoutError):
            pass

        # Memory tracker must still reflect only the first allocation.
        assert manager.current_memory_usage == 100

    @patch("dacs_utils.release_buffer", side_effect=_fake_release)
    @patch("dacs_utils.alloc_buffer", side_effect=_fake_alloc)
    async def test_backpressure_unblocks_after_release(self, mock_alloc, _mock_rel):
        """A blocked acquire must proceed once enough memory is released."""
        manager = BoundedBufferManager(max_memory_limit=100)

        buf1 = await manager.acquire_buffer(100)

        # Start a second acquire — it should block.
        acquire_task = asyncio.create_task(manager.acquire_buffer(50))
        await asyncio.sleep(0.1)
        assert not acquire_task.done(), "acquire_buffer should be blocked, not completed"

        # Release the first buffer → the blocked acquire should now complete.
        await manager.release_buffer(buf1)

        buf2 = await asyncio.wait_for(acquire_task, timeout=1.0)
        assert buf2 is not None
        assert manager.current_memory_usage == 50
