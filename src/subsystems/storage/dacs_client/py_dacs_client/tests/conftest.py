#!/usr/bin/env python3
"""
Pytest configuration and fixtures for DACS adapter tests (blobfile and boostedblob).

This file contains session-level hooks that are guaranteed to run
at the end of the test session.
"""

import logging
import os
import uuid

from py_dacs_client.dacs_client import normalize_azure_path


# Get the Azure blob base path from environment or use default
_AZURE_BLOB_BASE_PATH = os.environ.get(
    "AZURE_BLOB_PATH", "az://storageinterceptionpr1.blob.core.windows.net/dataset/dacs_blobfile"
)

# Generate a unique session ID to isolate this test run from other concurrent runs
# This prevents conflicts when multiple users/CI jobs run tests simultaneously
TEST_SESSION_ID = os.environ.get("PYTEST_SESSION_ID", str(uuid.uuid4())[:8])

# Create the unique session base path that all tests will use
# Normalize to az:// format - the adapters accept this directly
# Each test module appends its own adapter-specific sub-path (e.g., /blobfile, /boostedblob)
_raw_path = f"{_AZURE_BLOB_BASE_PATH}/session_{TEST_SESSION_ID}"
BASE_BLOB_PATH = normalize_azure_path(_raw_path)

print(f"Test session ID: {TEST_SESSION_ID}")
print(f"Test base blob path: {BASE_BLOB_PATH}")


def pytest_collection_modifyitems(items):
    """Reorder tests so that test_shutdown.py runs last.

    The native shutdown tests call ContentHandler::GetInstance().HandleShutdown()
    which triggers the C++ singleton constructor and caches the config. Running
    them last prevents contaminating the config state for other test suites
    (blobfile/boostedblob adapters) that need write-mode support.
    """
    shutdown_tests = []
    other_tests = []
    for item in items:
        if "test_shutdown" in item.nodeid:
            shutdown_tests.append(item)
        else:
            other_tests.append(item)
    items[:] = other_tests + shutdown_tests


def pytest_sessionfinish(session, exitstatus):
    """Clean up all test blobs after all tests complete.

    This hook is guaranteed to run at the end of the pytest session,
    regardless of test success/failure.

    Note: We use the Azure Storage SDK directly for cleanup because the
    DACS adapters intentionally do not support delete operations
    (e.g., bf.remove() and bf.rmtree() raise OperationNotSupportedError).
    This is by design to prevent accidental data loss in production usage.
    """
    import sys

    # Use print() to ensure visibility (logging may not be configured)
    print(f"\n{'='*60}")
    print(f"pytest_sessionfinish: Cleaning up test session {TEST_SESSION_ID}")
    print(f"Cleaning up test blobs under {BASE_BLOB_PATH}")
    print(f"Exit status: {exitstatus}")
    print(f"{'='*60}")
    sys.stdout.flush()

    try:
        # Using Azure SDK directly since the DACS adapters don't support
        # delete operations (remove/rmtree raise OperationNotSupportedError)
        print("Importing Azure SDK...")
        sys.stdout.flush()
        from azure.storage.blob import ContainerClient
        from azure.identity import DefaultAzureCredential

        print("Azure SDK imported successfully")
        sys.stdout.flush()

        # Parse the Azure blob path: az://account.blob.core.windows.net/container/prefix
        path = BASE_BLOB_PATH
        if path.startswith("az://"):
            path = path[5:]  # Remove 'az://'

        # Split into account URL and container/prefix
        parts = path.split("/", 1)
        account_url = f"https://{parts[0]}"

        if len(parts) > 1:
            container_and_prefix = parts[1].split("/", 1)
            container_name = container_and_prefix[0]
            prefix = container_and_prefix[1] if len(container_and_prefix) > 1 else ""
        else:
            logging.warning("Could not parse container from BASE_BLOB_PATH")
            return

        # Prefer access token if provided, else fall back to DefaultAzureCredential
        access_token = os.environ.get("SI_azBlobAccessToken")
        if access_token:
            from azure.core.credentials import AccessToken, TokenCredential

            class SimpleTokenCredential(TokenCredential):
                def __init__(self, token):
                    self._token = token

                def get_token(self, *scopes, **kwargs):
                    return AccessToken(self._token, 3600)

            credential = SimpleTokenCredential(access_token)
            print("Using provided access token for Azure cleanup.")
        else:
            credential = DefaultAzureCredential()
            print("Using DefaultAzureCredential for Azure cleanup.")

        container_client = ContainerClient(
            account_url=account_url, container_name=container_name, credential=credential
        )

        # List and delete all blobs with the test prefix
        deleted_count = 0
        blob_list = container_client.list_blobs(name_starts_with=prefix)

        print(f"Listing blobs with prefix: {prefix}")
        for blob in blob_list:
            try:
                container_client.delete_blob(blob.name)
                deleted_count += 1
                print(f"  Deleted: {blob.name}")
            except Exception as e:
                print(f"  Failed to delete {blob.name}: {e}")

        print(f"Cleanup complete. Deleted {deleted_count} test blobs.")

    except ImportError as e:
        print(
            f"WARNING: azure-storage-blob or azure-identity not installed. "
            f"Test blobs were not cleaned up. Error: {e}\n"
            f"Install with: pip install azure-storage-blob azure-identity"
        )
        sys.stdout.flush()
    except Exception as e:
        import traceback

        print(f"WARNING: Failed to clean up test blobs: {e}")
        print(f"Exception type: {type(e).__name__}")
        traceback.print_exc()
        sys.stdout.flush()
