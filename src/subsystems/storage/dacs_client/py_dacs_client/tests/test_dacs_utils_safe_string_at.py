#!/usr/bin/env python3
"""Tests for safe native-buffer-to-bytes copies."""

import ctypes
import gc

import pytest

from py_dacs_client import dacs_utils


def test_safe_string_at_reads_exact_size_including_embedded_nul():
    buffer = ctypes.create_string_buffer(b"abcdef\0XYZ")

    assert dacs_utils.safe_string_at(buffer, 10) == b"abcdef\0XYZ"


def test_safe_string_at_returns_empty_for_zero_size():
    buffer = ctypes.create_string_buffer(b"abcdef")

    assert dacs_utils.safe_string_at(buffer, 0) == b""


def test_safe_string_at_rejects_negative_size():
    buffer = ctypes.create_string_buffer(b"abcdef")

    with pytest.raises(ValueError, match="negative size"):
        dacs_utils.safe_string_at(buffer, -1)


def test_safe_string_at_uses_py_ssize_t_size():
    assert dacs_utils._PyBytes_FromStringAndSize.argtypes == [
        ctypes.c_void_p,
        ctypes.c_ssize_t,
    ]
    assert dacs_utils._PyBytes_FromStringAndSize.restype is ctypes.py_object


@pytest.mark.parametrize(
    "read_size",
    [
        2**31,
        2**32 + 1,
    ],
)
def test_safe_string_at_allocates_large_py_ssize_t_lengths(read_size):
    data = dacs_utils.safe_string_at(None, read_size)
    try:
        assert len(data) == read_size
    finally:
        del data
        gc.collect()
