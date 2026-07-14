#!/usr/bin/env python3
# Copyright (c) Microsoft Corporation.
# Licensed under the Apache License, Version 2.0.

# ---------------------------------------------------------------
# DACS Client - Overwrite (Consistent Mode) Tests for DACSBlobfileAdapter
#
# These tests verify overwrite support when consistent_mode=True.
# Consistent mode is initialized via the conftest.py fixture in this directory.
# NOTE: Must run in a separate pytest invocation from efficient-mode tests
# because both the Python _ClientManager singleton and the C++ config
# singleton cache values on first load.
# ---------------------------------------------------------------

import logging
import os
import sys
import signal
import faulthandler
import time
import pytest

# Enable faulthandler to get Python traceback on segfault/abort
faulthandler.enable()

def _signal_handler(signum, frame):
    import traceback
    print(f"\n{'='*60}")
    print(f"SIGNAL {signum} received!")
    traceback.print_stack(frame)
    print(f"{'='*60}")
    sys.stdout.flush()
    sys.exit(1)

signal.signal(signal.SIGABRT, _signal_handler)

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

import dacs_blobfile_adapter as bf
from dacs_blobfile_adapter import (
    DACSNotFoundError,
    OperationNotSupportedError,
)

# Import the unique session path from conftest.py
from conftest import BASE_BLOB_PATH, TEST_SESSION_ID

logger = logging.getLogger(__name__)

# Use a sub-path specific to overwrite tests
AZURE_BLOB_PATH = f"{BASE_BLOB_PATH}/overwrite"


class TestOverwriteNewFile:
    """Test creating new files in consistent mode (O_TRUNC on non-existent file)."""

    def test_create_new_file_consistent_mode(self):
        """New file creation should work in consistent mode (O_TRUNC + O_CREAT)."""
        test_data = "brand new file in consistent mode"
        blob_path = f"{AZURE_BLOB_PATH}/test_new_file_{id(self)}.txt"

        with bf.DACSBlobfileAdapter(blob_path, "w", consistent_mode=True) as f:
            bytes_written = f.write(test_data)

        assert bytes_written == len(test_data)

        # Read back and verify
        with bf.DACSBlobfileAdapter(blob_path, "r") as f:
            content = f.read()
        assert content == test_data

    def test_create_new_binary_file_consistent_mode(self):
        """New binary file creation should work in consistent mode."""
        test_data = b"\x00\x01\x02\x03binary data in consistent mode\xff\xfe"
        blob_path = f"{AZURE_BLOB_PATH}/test_new_binary_{id(self)}.bin"

        with bf.DACSBlobfileAdapter(blob_path, "wb") as f:
            bytes_written = f.write(test_data)

        assert bytes_written == len(test_data)

        with bf.DACSBlobfileAdapter(blob_path, "rb") as f:
            content = f.read()
        assert content == test_data


class TestOverwriteExistingFile:
    """Test overwriting existing files in consistent mode."""

    def test_overwrite_existing_file(self):
        """Write V1, close, overwrite with V2, read back V2."""
        blob_path = f"{AZURE_BLOB_PATH}/test_overwrite_{id(self)}.txt"

        # Write V1
        v1_data = "version 1 data"
        with bf.DACSBlobfileAdapter(blob_path, "w") as f:
            f.write(v1_data)

        # Verify V1
        with bf.DACSBlobfileAdapter(blob_path, "r") as f:
            content = f.read()
        assert content == v1_data

        # Overwrite with V2
        v2_data = "version 2 data - completely different"
        with bf.DACSBlobfileAdapter(blob_path, "w") as f:
            f.write(v2_data)

        # Read back — should be V2, not V1
        with bf.DACSBlobfileAdapter(blob_path, "r") as f:
            content = f.read()
        assert content == v2_data, f"Expected V2 data but got: {content}"

    def test_overwrite_with_smaller_data(self):
        """Overwrite a large file with smaller data — read should return only new data."""
        blob_path = f"{AZURE_BLOB_PATH}/test_overwrite_smaller_{id(self)}.txt"

        # Write large V1
        v1_data = "A" * 10000
        with bf.DACSBlobfileAdapter(blob_path, "w") as f:
            f.write(v1_data)

        # Overwrite with small V2
        v2_data = "tiny"
        with bf.DACSBlobfileAdapter(blob_path, "w") as f:
            f.write(v2_data)

        # Read back — size should be V2's size, not V1's
        with bf.DACSBlobfileAdapter(blob_path, "r") as f:
            content = f.read()
        assert content == v2_data
        assert len(content) == len(v2_data)

    def test_overwrite_with_larger_data(self):
        """Overwrite a small file with larger data."""
        blob_path = f"{AZURE_BLOB_PATH}/test_overwrite_larger_{id(self)}.txt"

        # Write small V1
        v1_data = "small"
        with bf.DACSBlobfileAdapter(blob_path, "w") as f:
            f.write(v1_data)

        # Overwrite with large V2
        v2_data = "B" * 10000
        with bf.DACSBlobfileAdapter(blob_path, "w") as f:
            f.write(v2_data)

        with bf.DACSBlobfileAdapter(blob_path, "r") as f:
            content = f.read()
        assert content == v2_data

    def test_overwrite_binary_file(self):
        """Overwrite binary file and verify content integrity."""
        blob_path = f"{AZURE_BLOB_PATH}/test_overwrite_binary_{id(self)}.bin"

        v1_data = bytes(range(256))
        with bf.DACSBlobfileAdapter(blob_path, "wb") as f:
            f.write(v1_data)

        v2_data = bytes(range(255, -1, -1))  # reversed
        with bf.DACSBlobfileAdapter(blob_path, "wb") as f:
            f.write(v2_data)

        with bf.DACSBlobfileAdapter(blob_path, "rb") as f:
            content = f.read()
        assert content == v2_data

    def test_multiple_overwrites(self):
        """Overwrite same file multiple times — final read should be last version."""
        blob_path = f"{AZURE_BLOB_PATH}/test_multi_overwrite_{id(self)}.txt"

        for i in range(5):
            data = f"version {i}"
            with bf.DACSBlobfileAdapter(blob_path, "w") as f:
                f.write(data)

        # Only the last version should be readable
        with bf.DACSBlobfileAdapter(blob_path, "r") as f:
            content = f.read()
        assert content == "version 4"


class TestOverwriteStatAndMetadata:
    """Test that stat() reflects the new file after overwrite."""

    def test_stat_reflects_new_size_after_overwrite(self):
        """After overwrite, stat().size should reflect the new data size."""
        blob_path = f"{AZURE_BLOB_PATH}/test_stat_overwrite_{id(self)}.txt"

        # Write V1 (large)
        v1_data = "X" * 5000
        with bf.DACSBlobfileAdapter(blob_path, "w") as f:
            f.write(v1_data)

        stat1 = bf.stat(blob_path)
        assert stat1.size == len(v1_data)

        # Overwrite with V2 (small)
        v2_data = "Y" * 100
        with bf.DACSBlobfileAdapter(blob_path, "w") as f:
            f.write(v2_data)

        stat2 = bf.stat(blob_path)
        assert stat2.size == len(v2_data), f"Expected size {len(v2_data)} but got {stat2.size}"

    def test_exists_after_overwrite(self):
        """File should exist after overwrite."""
        blob_path = f"{AZURE_BLOB_PATH}/test_exists_overwrite_{id(self)}.txt"

        with bf.DACSBlobfileAdapter(blob_path, "w") as f:
            f.write("v1")

        assert bf.exists(blob_path)

        with bf.DACSBlobfileAdapter(blob_path, "w") as f:
            f.write("v2")

        assert bf.exists(blob_path)


class TestCopyOverwriteConsistentMode:
    """Test that copy(overwrite=True) works for remote destinations in consistent mode."""

    def test_copy_overwrite_local_to_remote(self):
        """copy() with overwrite=True to a remote destination should work in consistent mode."""
        import tempfile

        blob_path = f"{AZURE_BLOB_PATH}/test_copy_overwrite_{id(self)}.txt"

        # Create remote file first
        with bf.DACSBlobfileAdapter(blob_path, "w") as f:
            f.write("original data")

        # Create local source with new content
        with tempfile.NamedTemporaryFile(mode="w", suffix=".txt", delete=False) as tmp:
            tmp.write("overwritten via copy")
            local_src = tmp.name

        try:
            bf.copy(local_src, blob_path, overwrite=True)

            # Read back — should be the new content
            with bf.DACSBlobfileAdapter(blob_path, "r") as f:
                content = f.read()
            assert content == "overwritten via copy"
        finally:
            os.unlink(local_src)


class TestExternalWriteDetection:
    """Test that DACS reads fresh data after an external (out-of-band) blob write.

    In consistent mode, attr cache is disabled so every GetProperties goes to blob,
    picks up the new version, and the read uses the new version key → cache miss → blob read.
    This ensures DACS never serves stale cached data after an external writer updates the blob.
    """

    @staticmethod
    def _get_azure_container_client():
        """Create an Azure ContainerClient for direct blob operations."""
        from azure.storage.blob import ContainerClient

        path = BASE_BLOB_PATH
        if path.startswith("az://"):
            path = path[5:]
        parts = path.split("/", 1)
        account_url = f"https://{parts[0]}"
        container_and_prefix = parts[1].split("/", 1)
        container_name = container_and_prefix[0]

        sas_token = os.environ.get("STORAGE_INTCPT_SAS_TOKEN") if os.environ.get("USE_SAS_TOKEN", "false") == "true" else None
        if sas_token:
            from azure.storage.blob import ContainerClient as CC
            return CC(account_url=account_url, container_name=container_name, credential=sas_token.strip())
        else:
            from azure.identity import DefaultAzureCredential
            return ContainerClient(account_url=account_url, container_name=container_name, credential=DefaultAzureCredential())

    @staticmethod
    def _to_azure_blob_path(blob_path):
        """Convert az:// path to the Azure blob path (strip account/container prefix)."""
        raw = blob_path[5:] if blob_path.startswith("az://") else blob_path
        parts = raw.split("/", 2)
        return parts[2] if len(parts) > 2 else ""

    def test_write_overwrite_external_write_read_all_correct(self):
        """Strict version flow: DACS write → read → DACS overwrite → read → external write → read.
        Every read must return the data from the most recent write, regardless of source."""
        blob_path = f"{AZURE_BLOB_PATH}/test_strict_version_flow_{id(self)}.txt"

        # V1: DACS write
        v1_data = "v1-dacs-initial"
        with bf.DACSBlobfileAdapter(blob_path, "w") as f:
            f.write(v1_data)
        with bf.DACSBlobfileAdapter(blob_path, "r") as f:
            assert f.read() == v1_data, "Read after V1 write should return V1"
        assert bf.stat(blob_path).size == len(v1_data)

        # V2: DACS overwrite
        v2_data = "v2-dacs-overwrite-longer-data"
        with bf.DACSBlobfileAdapter(blob_path, "w") as f:
            f.write(v2_data)
        with bf.DACSBlobfileAdapter(blob_path, "r") as f:
            assert f.read() == v2_data, "Read after V2 overwrite should return V2, not stale V1"
        assert bf.stat(blob_path).size == len(v2_data)

        # V3: External write via Azure SDK
        v3_data = "v3-external-azure-sdk"
        container_client = self._get_azure_container_client()
        container_client.upload_blob(self._to_azure_blob_path(blob_path), v3_data, overwrite=True)

        with bf.DACSBlobfileAdapter(blob_path, "r") as f:
            assert f.read() == v3_data, "Read after external V3 write should return V3, not stale V2"
        assert bf.stat(blob_path).size == len(v3_data)

        # V4: DACS overwrite after external write
        v4_data = "v4-dacs-after-external"
        with bf.DACSBlobfileAdapter(blob_path, "w") as f:
            f.write(v4_data)
        with bf.DACSBlobfileAdapter(blob_path, "r") as f:
            assert f.read() == v4_data, "Read after V4 overwrite should return V4, not stale V3"
        assert bf.stat(blob_path).size == len(v4_data)
