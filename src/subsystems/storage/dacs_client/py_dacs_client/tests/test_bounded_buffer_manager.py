#!/usr/bin/env python3
# Copyright (c) Microsoft Corporation.
# Licensed under the Apache License, Version 2.0.

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


@pytest.mark.skip(reason="BoundedBufferManager memory capping disabled temporarily, to be moved to C++")
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
    async def test_release_uses_tracked_size_not_mutated_len(self, _mock_rel):
        manager = BoundedBufferManager(max_memory_limit=200)

        buf = MutableLenBuffer(80)
        manager.current_memory_usage = 80
        manager._buffer_sizes[id(buf)] = 80

        # Simulate object whose __len__ no longer reflects allocation size.
        buf.set_size(5)
        await manager.release_buffer(buf)

        assert manager.current_memory_usage == 0


@pytest.mark.skip(reason="BoundedBufferManager memory capping temporarily disabled, to be moved to C++")
@pytest.mark.asyncio
class TestBoundedBufferManagerMultipleEventLoops:
    """Tests for calling BoundedBufferManager async functions from different event loops."""

    @patch("dacs_utils.release_buffer", side_effect=_fake_release)
    @patch("dacs_utils.alloc_buffer", side_effect=_fake_alloc)
    async def test_acquire_and_release_from_different_event_loops(self, _mock_alloc, _mock_rel):
        """
        Verify that BoundedBufferManager correctly handles async operations
        from multiple different event loops running in separate threads.
        """
        import concurrent.futures
        import threading

        manager = BoundedBufferManager(max_memory_limit=200)
        results = []
        errors = []
        results_lock = threading.Lock()

        def run_in_new_loop(acquire_size: int, thread_id: int):
            """Run acquire and release operations in a new event loop."""
            loop = asyncio.new_event_loop()
            asyncio.set_event_loop(loop)
            try:
                async def do_operations():
                    buf = await manager.acquire_buffer(acquire_size)
                    # Brief delay to ensure concurrent operations overlap
                    await asyncio.sleep(0.05)
                    await manager.release_buffer(buf)
                    return acquire_size

                result = loop.run_until_complete(do_operations())
                with results_lock:
                    results.append((thread_id, result))
            except Exception as e:
                with results_lock:
                    errors.append((thread_id, str(e)))
            finally:
                loop.close()

        # Launch multiple threads, each with its own event loop
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
            futures = [
                executor.submit(run_in_new_loop, 50, i)
                for i in range(4)
            ]
            concurrent.futures.wait(futures)

        # No errors should have occurred
        assert len(errors) == 0, f"Errors occurred: {errors}"
        # All operations should have completed
        assert len(results) == 4
        # Memory should be back to zero after all releases
        assert manager.current_memory_usage == 0

    @patch("dacs_utils.release_buffer", side_effect=_fake_release)
    @patch("dacs_utils.alloc_buffer", side_effect=_fake_alloc)
    async def test_concurrent_acquires_from_different_loops_respect_memory_limit(self, _mock_alloc, _mock_rel):
        """
        Verify that memory limits are respected when concurrent acquires
        come from different event loops (threads).
        """
        import concurrent.futures
        import threading

        manager = BoundedBufferManager(max_memory_limit=100)
        completed_count = [0]
        count_lock = threading.Lock()
        max_observed_usage = [0]

        def run_acquire_release_in_new_loop(acquire_size: int, thread_id: int):
            """Acquire a buffer, hold briefly, then release - all in a new event loop."""
            loop = asyncio.new_event_loop()
            asyncio.set_event_loop(loop)
            try:
                async def do_acquire_and_release():
                    buf = await manager.acquire_buffer(acquire_size)
                    with count_lock:
                        max_observed_usage[0] = max(max_observed_usage[0], manager.current_memory_usage)
                    # Hold the buffer briefly
                    await asyncio.sleep(0.05)
                    # Release the buffer so other waiters can proceed
                    await manager.release_buffer(buf)
                    with count_lock:
                        completed_count[0] += 1

                loop.run_until_complete(do_acquire_and_release())
            finally:
                loop.close()

        # Launch threads that will compete for limited memory
        # Total requested: 4 * 40 = 160, but limit is 100
        # Each thread acquires, holds briefly, then releases - allowing others to proceed
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
            futures = [
                executor.submit(run_acquire_release_in_new_loop, 40, i)
                for i in range(4)
            ]
            concurrent.futures.wait(futures, timeout=10.0)

        # All should have completed (eventually, via backpressure)
        assert completed_count[0] == 4, f"Only {completed_count[0]} threads completed"

        # Memory limit should never have been exceeded by more than a single
        # oversized allocation allowance (here, each request fits individually)
        assert max_observed_usage[0] <= 100, f"Max observed usage {max_observed_usage[0]} exceeded limit"

        # All buffers released, memory should be zero
        assert manager.current_memory_usage == 0

    @patch("dacs_utils.release_buffer", side_effect=_fake_release)
    @patch("dacs_utils.alloc_buffer", side_effect=_fake_alloc)
    async def test_interleaved_operations_from_main_and_secondary_loops(self, _mock_alloc, _mock_rel):
        """
        Verify that operations interleaved between the main test event loop
        and a secondary thread's event loop work correctly.
        """
        import threading

        manager = BoundedBufferManager(max_memory_limit=150)
        secondary_result = []
        secondary_error = []

        # Acquire from main loop first
        buf_main = await manager.acquire_buffer(60)
        assert manager.current_memory_usage == 60

        def secondary_loop_operations():
            loop = asyncio.new_event_loop()
            asyncio.set_event_loop(loop)
            try:
                async def do_secondary():
                    # Acquire from secondary loop
                    buf = await manager.acquire_buffer(50)
                    secondary_result.append(("acquired", manager.current_memory_usage))
                    await asyncio.sleep(0.05)
                    await manager.release_buffer(buf)
                    secondary_result.append(("released", manager.current_memory_usage))

                loop.run_until_complete(do_secondary())
            except Exception as e:
                secondary_error.append(str(e))
            finally:
                loop.close()

        # Run secondary operations in a thread
        thread = threading.Thread(target=secondary_loop_operations)
        thread.start()
        thread.join(timeout=5.0)

        assert len(secondary_error) == 0, f"Secondary loop error: {secondary_error}"
        assert len(secondary_result) == 2

        # After secondary acquires, usage should have been 60 + 50 = 110
        assert secondary_result[0] == ("acquired", 110)
        # After secondary releases, usage should be back to 60
        assert secondary_result[1] == ("released", 60)

        # Main usage should still reflect only main buffer
        assert manager.current_memory_usage == 60

        # Release main buffer
        await manager.release_buffer(buf_main)
        assert manager.current_memory_usage == 0


@pytest.mark.skip(reason="BoundedBufferManager memory capping temporarily disabled, to be moved to C++")
@pytest.mark.skipif(not hasattr(os, 'fork'), reason="fork() not available on this platform")
class TestBoundedBufferManagerForkHandling:
    """Tests for BoundedBufferManager fork safety."""

    @patch("dacs_utils.release_buffer", side_effect=_fake_release)
    @patch("dacs_utils.alloc_buffer", side_effect=_fake_alloc)
    def test_child_process_resets_memory_state_after_fork(self, _mock_alloc, _mock_rel):
        """
        Verify that after fork, the child process resets memory usage
        and buffer tracking to zero (parent's buffers don't exist in child).
        """
        import multiprocessing

        manager = BoundedBufferManager(max_memory_limit=200)

        # Acquire buffer in parent to set up some state
        buf = manager.acquire_buffer_sync(80)
        assert manager.current_memory_usage == 80
        assert len(manager._buffer_sizes) == 1

        # Use a pipe to communicate child's state back to parent
        parent_conn, child_conn = multiprocessing.Pipe()

        pid = os.fork()
        if pid == 0:
            # Child process
            try:
                # Trigger _ensure_condition which should detect fork and reset state
                manager._ensure_condition()

                # Send child's state to parent
                child_conn.send({
                    "memory_usage": manager.current_memory_usage,
                    "buffer_count": len(manager._buffer_sizes),
                    "condition_pid": manager._condition_pid,
                    "child_pid": os.getpid(),
                })
            except Exception as e:
                child_conn.send({"error": str(e)})
            finally:
                child_conn.close()
                os._exit(0)
        else:
            # Parent process
            child_conn.close()

            # Wait for child and get results
            result = parent_conn.recv()
            parent_conn.close()
            os.waitpid(pid, 0)

            assert "error" not in result, f"Child error: {result.get('error')}"

            # Child should have reset state
            assert result["memory_usage"] == 0, "Child memory usage should be 0 after fork"
            assert result["buffer_count"] == 0, "Child buffer count should be 0 after fork"
            assert result["condition_pid"] == result["child_pid"], "Child condition_pid should match child PID"

            # Parent state should be unchanged
            assert manager.current_memory_usage == 80
            assert len(manager._buffer_sizes) == 1

            # Cleanup parent
            manager.release_buffer_sync(buf)

    @patch("dacs_utils.release_buffer", side_effect=_fake_release)
    @patch("dacs_utils.alloc_buffer", side_effect=_fake_alloc)
    def test_child_can_acquire_buffers_independently_after_fork(self, _mock_alloc, _mock_rel):
        """
        Verify that a child process can acquire and release buffers
        independently after fork, without affecting parent.
        """
        import multiprocessing

        manager = BoundedBufferManager(max_memory_limit=100)

        # Fill parent's limit
        buf_parent = manager.acquire_buffer_sync(100)
        assert manager.current_memory_usage == 100

        parent_conn, child_conn = multiprocessing.Pipe()

        pid = os.fork()
        if pid == 0:
            # Child process
            try:
                # Child should be able to acquire despite parent being "full"
                # because fork resets child's accounting
                buf_child = manager.acquire_buffer_sync(50)
                usage_after_acquire = manager.current_memory_usage

                manager.release_buffer_sync(buf_child)
                usage_after_release = manager.current_memory_usage

                child_conn.send({
                    "usage_after_acquire": usage_after_acquire,
                    "usage_after_release": usage_after_release,
                })
            except Exception as e:
                child_conn.send({"error": str(e)})
            finally:
                child_conn.close()
                os._exit(0)
        else:
            # Parent process
            child_conn.close()

            result = parent_conn.recv()
            parent_conn.close()
            os.waitpid(pid, 0)

            assert "error" not in result, f"Child error: {result.get('error')}"

            # Child should have been able to acquire (starting from 0)
            assert result["usage_after_acquire"] == 50
            assert result["usage_after_release"] == 0

            # Parent usage unchanged
            assert manager.current_memory_usage == 100

            # Cleanup
            manager.release_buffer_sync(buf_parent)

    @patch("dacs_utils.release_buffer", side_effect=_fake_release)
    @patch("dacs_utils.alloc_buffer", side_effect=_fake_alloc)
    def test_multiple_forks_each_get_independent_state(self, _mock_alloc, _mock_rel):
        """
        Verify that multiple child processes each get their own independent state.
        """
        import multiprocessing

        manager = BoundedBufferManager(max_memory_limit=200)
        manager.acquire_buffer_sync(50)

        results = []
        pipes = []

        # Fork multiple children
        for i in range(3):
            parent_conn, child_conn = multiprocessing.Pipe()
            pipes.append((parent_conn, child_conn))

            pid = os.fork()
            if pid == 0:
                # Child process
                try:
                    # Each child acquires a different amount
                    acquire_amount = (i + 1) * 20
                    buf = manager.acquire_buffer_sync(acquire_amount)
                    child_conn.send({
                        "child_index": i,
                        "memory_usage": manager.current_memory_usage,
                        "acquired": acquire_amount,
                    })
                    manager.release_buffer_sync(buf)
                except Exception as e:
                    child_conn.send({"error": str(e), "child_index": i})
                finally:
                    child_conn.close()
                    os._exit(0)
            else:
                # Parent continues to fork more children
                child_conn.close()
                results.append((pid, parent_conn))

        # Parent collects results
        for pid, parent_conn in results:
            result = parent_conn.recv()
            parent_conn.close()
            os.waitpid(pid, 0)

            assert "error" not in result, f"Child {result.get('child_index')} error: {result.get('error')}"

            # Each child should have started fresh and only see its own allocation
            expected_usage = result["acquired"]
            assert result["memory_usage"] == expected_usage, (
                f"Child {result['child_index']} expected usage {expected_usage}, got {result['memory_usage']}"
            )

        # Parent still has its original state
        assert manager.current_memory_usage == 50

@pytest.mark.skip(reason="BoundedBufferManager memory capping temporarily disabled, to be moved to C++")
def test_max_memory_limit_must_be_positive():
    with pytest.raises(ValueError, match="max_memory_limit must be > 0"):
        BoundedBufferManager(max_memory_limit=0)

    with pytest.raises(ValueError, match="max_memory_limit must be > 0"):
        BoundedBufferManager(max_memory_limit=-1)
