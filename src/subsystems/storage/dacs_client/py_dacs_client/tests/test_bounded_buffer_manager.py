#!/usr/bin/env python3
"""
Tests for BoundedBufferManager memory limiting behavior.

Validates that:
  - acquire_buffer blocks when the memory limit is reached
  - current_memory_usage is not corrupted when an acquire is denied
    - condition-based back-pressure unblocks waiting acquirers after release
    - oversized single requests, allocation failures, and release edge cases are handled safely
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


class MutableLenBuffer(FakeBuffer):
    def set_size(self, size):
        self._size = size


def _fake_alloc(size):
    return FakeBuffer(size)


def _fake_release(buf):
    pass


class UnsizedBuffer:
    def __bool__(self):
        return True


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

    @patch("dacs_utils.release_buffer", side_effect=_fake_release)
    @pytest.mark.parametrize(
        "buffer_obj, starting_usage, expected_usage",
        [
            (None, 20, 20),
            (UnsizedBuffer(), 75, 75),
        ],
    )
    async def test_release_edge_cases_keep_accounting_stable(
        self, _mock_rel, buffer_obj, starting_usage, expected_usage
    ):
        manager = BoundedBufferManager(max_memory_limit=100)
        manager.current_memory_usage = starting_usage

        await manager.release_buffer(buffer_obj)
        assert manager.current_memory_usage == expected_usage

    @patch("dacs_utils.release_buffer", side_effect=_fake_release)
    @patch("dacs_utils.alloc_buffer", side_effect=_fake_alloc)
    async def test_oversized_single_request_is_allowed_and_released(self, _mock_alloc, _mock_rel):
        manager = BoundedBufferManager(max_memory_limit=100)

        # Requests larger than cap are allowed by design to avoid deadlock.
        buf = await manager.acquire_buffer(150)
        assert buf is not None
        assert manager.current_memory_usage == 150

        await manager.release_buffer(buf)
        assert manager.current_memory_usage == 0

    @patch("dacs_utils.release_buffer", side_effect=_fake_release)
    @patch("dacs_utils.alloc_buffer", side_effect=_fake_alloc)
    async def test_multiple_waiters_unblock_under_pressure(self, _mock_alloc, _mock_rel):
        manager = BoundedBufferManager(max_memory_limit=100)

        initial = await manager.acquire_buffer(100)

        waiter_a = asyncio.create_task(manager.acquire_buffer(40))
        waiter_b = asyncio.create_task(manager.acquire_buffer(60))
        await asyncio.sleep(0.1)
        assert not waiter_a.done()
        assert not waiter_b.done()

        # Freeing initial buffer should unblock both waiters eventually.
        await manager.release_buffer(initial)

        buf_a = await asyncio.wait_for(waiter_a, timeout=1.0)
        buf_b = await asyncio.wait_for(waiter_b, timeout=1.0)

        assert buf_a is not None
        assert buf_b is not None
        assert manager.current_memory_usage == 100

        await manager.release_buffer(buf_a)
        await manager.release_buffer(buf_b)
        assert manager.current_memory_usage == 0

    @patch("dacs_utils.release_buffer", side_effect=_fake_release)
    @patch("dacs_utils.alloc_buffer", side_effect=MemoryError("alloc failed"))
    async def test_alloc_failure_does_not_inflate_accounting(self, _mock_alloc, _mock_rel):
        manager = BoundedBufferManager(max_memory_limit=100)

        with pytest.raises(MemoryError, match="alloc failed"):
            await manager.acquire_buffer(50)

        assert manager.current_memory_usage == 0
        assert manager._buffer_sizes == {}

    @patch("dacs_utils.release_buffer", side_effect=_fake_release)
    async def test_release_uses_tracked_size_not_mutated_len(self, _mock_rel):
        manager = BoundedBufferManager(max_memory_limit=200)

        buf = MutableLenBuffer(80)
        manager.current_memory_usage = 80
        manager._buffer_sizes[id(buf)] = 80

        # Simulate object whose __len__ no longer reflects allocation size.
        buf.set_size(5)
        await manager.release_buffer(buf)

        assert manager.current_memory_usage == 0


def test_max_memory_limit_must_be_positive():
    with pytest.raises(ValueError, match="max_memory_limit must be > 0"):
        BoundedBufferManager(max_memory_limit=0)

    with pytest.raises(ValueError, match="max_memory_limit must be > 0"):
        BoundedBufferManager(max_memory_limit=-1)
