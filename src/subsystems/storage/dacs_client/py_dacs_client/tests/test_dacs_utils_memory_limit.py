#!/usr/bin/env python3
"""Tests for get_memory_limit() fallback behavior."""

import pytest
from py_dacs_client import dacs_utils

class TestGetMemoryLimit:
    def test_uses_dacs_memory_mb_when_set(self, monkeypatch):
        monkeypatch.setenv("DACS_MEMORY_MB", "4096")
        monkeypatch.setenv("DACS_CPU_CORES", "8")

        assert dacs_utils.get_memory_limit() == 4096 * 1024 * 1024

    @pytest.mark.parametrize(
        "memory_env,cpu_cores,expected_mb",
        [
            (None, "8", 2048),
            ("invalid", "8", 2048),
            ("0", "8", 2048),
            ("-1", "8", 2048),
        ],
    )
    def test_falls_back_to_core_based_limit(self, monkeypatch, memory_env, cpu_cores, expected_mb):
        if memory_env is None:
            monkeypatch.delenv("DACS_MEMORY_MB", raising=False)
        else:
            monkeypatch.setenv("DACS_MEMORY_MB", memory_env)
        monkeypatch.setenv("DACS_CPU_CORES", cpu_cores)

        assert dacs_utils.get_memory_limit() == expected_mb * 1024 * 1024

    def test_fallback_uses_os_cpu_count_when_cpu_env_missing(self, monkeypatch):
        monkeypatch.delenv("DACS_MEMORY_MB", raising=False)
        monkeypatch.delenv("DACS_CPU_CORES", raising=False)
        monkeypatch.setattr(dacs_utils.os, "cpu_count", lambda: 128)

        assert dacs_utils.get_memory_limit() == (128 * 256) * 1024 * 1024
