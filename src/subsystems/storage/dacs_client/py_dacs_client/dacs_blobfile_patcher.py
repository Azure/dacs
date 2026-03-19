#!/usr/bin/env python3
# --------------------------------------------------------------------------------------------------------------------
# DACS Blobfile Patcher
#
# Monkey-patches blobfile to use DACS Client for az:// paths.
# Controlled by an explicit initialization call or environment variable.
#
# Usage:
#     # Option A: Explicit initialization
#     import blobfile as bf
#     from py_dacs_client.dacs_blobfile_patcher import enable_dacs_backend
#
#     enable_dacs_backend()
#
#     with bf.BlobFile("az://myaccount/container/file.txt", "rb") as f:
#         data = f.read()  # Uses DACSBlobfileAdapter
#
#     # Option B: Auto-enable via environment variable
#     # export BLOBFILE_USE_DACS=1
#     import blobfile as bf
#     from py_dacs_client.dacs_blobfile_patcher import enable_dacs_backend  # Auto-patches on import
#
# --------------------------------------------------------------------------------------------------------------------

import errno
import os
import re
import warnings
import blobfile as bf
from typing import Optional

from py_dacs_client.dacs_blobfile_exception_handler import convert_and_raise

_BLOBFILE_PATCHED = False
_BLOBFILE_ORIGINAL_FUNCTIONS = {}
_BLOBFILE_ORIGINAL_MODULE_CLASS = None

# Supported APIs - only these get patched with routing logic
# Everything else raises OperationNotSupportedError
DACS_SUPPORTED_APIS = {'BlobFile', 'copy', 'exists', 'isdir', 'join', 'listdir', 'scandir', 'stat'}


def enable_dacs_backend():
    """
    Enable DACSBlobfileAdapter backend for blobfile Azure operations.

    After calling this function:
    - Supported APIs route az:// paths through DACSBlobfileAdapter
    - Supported APIs route non-Azure paths through original blobfile
    - Unsupported APIs raise OperationNotSupportedError

    Example:
        import blobfile as bf
        from py_dacs_client.dacs_blobfile_patcher import enable_dacs_backend

        # Enable DACSBlobfileAdapter for Azure operations
        enable_dacs_backend()

        # Now all bf.* calls use DACSBlobfileAdapter for Azure paths
        with bf.BlobFile("az://myaccount/container/file.txt", "rb") as f:
            data = f.read()

        for filename in bf.listdir("az://myaccount/container/"):
            print(filename)
    """
    global _BLOBFILE_PATCHED, _BLOBFILE_ORIGINAL_FUNCTIONS, _BLOBFILE_ORIGINAL_MODULE_CLASS

    if _BLOBFILE_PATCHED:
        return  # Already patched

    from py_dacs_client.dacs_blobfile_adapter import (
        DACSBlobfileAdapter,
        copy as dacs_copy,
        exists as dacs_exists,
        isdir as dacs_isdir,
        listdir as dacs_listdir,
        scandir as dacs_scandir,
        stat as dacs_stat,
    )
    from py_dacs_client.dacs_exceptions import OperationNotSupportedError

    # Save original functions
    _BLOBFILE_ORIGINAL_FUNCTIONS = {
        'BlobFile': bf.BlobFile,
        'copy': bf.copy,
        'exists': bf.exists,
        'isdir': bf.isdir,
        'join': bf.join,
        'listdir': bf.listdir,
        'scandir': bf.scandir,
        'stat': bf.stat,
        'Error': bf.Error,
    }

    def _is_azure_path(path):
        """Check if path is an Azure blob path."""
        return isinstance(path, str) and path.startswith("az://")

    # --- Patched functions ---

    class _BlobFileWrapper:
        """Wrapper that intercepts errors from DACSBlobfileAdapter
        and converts them to blobfile-compatible exceptions."""

        def __init__(self, adapter, original_path):
            self._adapter = adapter
            self._original_path = original_path

        def __enter__(self):
            try:
                self._adapter.__enter__()
                return self
            except Exception as e:
                convert_and_raise(e, original_path=self._original_path)

        def __exit__(self, *args):
            return self._adapter.__exit__(*args)

        def __getattr__(self, name):
            attr = getattr(self._adapter, name)
            if not callable(attr):
                return attr

            def wrapper(*args, **kwargs):
                try:
                    return attr(*args, **kwargs)
                except Exception as e:
                    convert_and_raise(e, original_path=self._original_path)

            return wrapper

    def patched_BlobFile(path, mode="r", streaming=None, buffer_size=None,
                         cache_dir=None, file_size=None, **kwargs):
        if _is_azure_path(path):
            # Filter kwargs to only those supported by DACSBlobfileAdapter
            dacs_kwargs = {k: v for k, v in kwargs.items() if k in ('write_mode',)}
            try:
                adapter = DACSBlobfileAdapter(
                    path=path, mode=mode, streaming=streaming,
                    buffer_size=buffer_size, cache_dir=cache_dir,
                    file_size=file_size, **dacs_kwargs
                )
                return _BlobFileWrapper(adapter, path)
            except Exception as e:
                convert_and_raise(e, original_path=path)
        return _BLOBFILE_ORIGINAL_FUNCTIONS['BlobFile'](
            path, mode, streaming, buffer_size, cache_dir, file_size, **kwargs
        )

    def patched_exists(path):
        if _is_azure_path(path):
            try:
                return dacs_exists(path)
            except Exception as e:
                convert_and_raise(e, original_path=path)
        return _BLOBFILE_ORIGINAL_FUNCTIONS['exists'](path)

    _URI_SCHEME_RE = re.compile(r'^[a-zA-Z][a-zA-Z0-9+\-.]+://')

    def _is_local_path(path):
        """Check if path is a local filesystem path (not a URI scheme like az://, gs://).
        
        Requires 2+ letter scheme prefix
        """
        return isinstance(path, str) and _URI_SCHEME_RE.match(path) is None

    def patched_copy(src, dst, overwrite=False, parallel=False, parallel_executor=None,
                     return_md5=False, dst_version=None):
        src_supported = _is_azure_path(src) or _is_local_path(src)
        dst_supported = _is_azure_path(dst) or _is_local_path(dst)

        # Route to dacs_copy only when both paths are supported (azure or local)
        if src_supported and dst_supported:
            try:
                unsupported = []
                if parallel:
                    unsupported.append("parallel")
                if parallel_executor is not None:
                    unsupported.append("parallel_executor")
                if dst_version is not None:
                    unsupported.append("dst_version")
                if unsupported:
                    warnings.warn(
                        f"DACS copy does not support the following arguments, they will be ignored: "
                        f"{', '.join(unsupported)}",
                        UserWarning,
                        stacklevel=2
                    )
                return dacs_copy(src, dst, overwrite=overwrite, return_md5=return_md5)
            except Exception as e:
                convert_and_raise(e, original_path=src)

        # Fallback to native blobfile for non-azure/non-local paths (e.g. gs://)
        return _BLOBFILE_ORIGINAL_FUNCTIONS['copy'](
            src, dst, overwrite=overwrite, parallel=parallel,
            parallel_executor=parallel_executor, return_md5=return_md5,
            dst_version=dst_version
        )

    def patched_listdir(path, shard_prefix_length=0):
        if _is_azure_path(path):
            if shard_prefix_length != 0:
                raise ValueError(
                    "shard_prefix_length is not supported for Azure paths with DACSBlobfileAdapter. "
                    "Got shard_prefix_length=%r"
                    % shard_prefix_length
                )
            try:
                return dacs_listdir(path)
            except Exception as e:
                convert_and_raise(e, original_path=path)
        return _BLOBFILE_ORIGINAL_FUNCTIONS['listdir'](path, shard_prefix_length)

    def patched_scandir(path, shard_prefix_length=0):
        if _is_azure_path(path):
            if shard_prefix_length != 0:
                raise ValueError(
                    "shard_prefix_length is not supported for Azure paths with DACSBlobfileAdapter. "
                    "Got shard_prefix_length=%r"
                    % shard_prefix_length
                )
            try:
                return dacs_scandir(path)
            except Exception as e:
                convert_and_raise(e, original_path=path)
        return _BLOBFILE_ORIGINAL_FUNCTIONS['scandir'](path, shard_prefix_length)

    def patched_stat(path):
        if _is_azure_path(path):
            try:
                return dacs_stat(path)
            except Exception as e:
                convert_and_raise(e, original_path=path)
        return _BLOBFILE_ORIGINAL_FUNCTIONS['stat'](path)

    def patched_isdir(path):
        if _is_azure_path(path):
            try:
                return dacs_isdir(path)
            except Exception as e:
                convert_and_raise(e, original_path=path)
        return _BLOBFILE_ORIGINAL_FUNCTIONS['isdir'](path)

    # join: passthrough to native blobfile — pure string manipulation, no I/O
    patched_join = _BLOBFILE_ORIGINAL_FUNCTIONS['join']

    # Apply patches to supported APIs
    bf.BlobFile = patched_BlobFile
    bf.copy = patched_copy
    bf.exists = patched_exists
    bf.isdir = patched_isdir
    bf.join = patched_join
    bf.listdir = patched_listdir
    bf.scandir = patched_scandir
    bf.stat = patched_stat
    bf.Error = OSError

    # --- Intercept unsupported APIs via __getattribute__ ---

    _BLOBFILE_ORIGINAL_MODULE_CLASS = bf.__class__

    class _PatchedBlobfileModule(_BLOBFILE_ORIGINAL_MODULE_CLASS):
        """Module class that raises error for unsupported APIs."""

        def __getattribute__(self, name):
            # Allow internal attributes
            if name.startswith('_'):
                return super().__getattribute__(name)

            # Allow supported APIs and Error
            if name in DACS_SUPPORTED_APIS or name == 'Error':
                return super().__getattribute__(name)

            # Try to get the attribute
            try:
                attr = super().__getattribute__(name)
            except AttributeError:
                raise  # Let original AttributeError propagate

            # If it's callable and not supported, wrap with error
            if callable(attr):
                def unsupported_wrapper(*args, **kwargs):
                    raise OperationNotSupportedError(
                        f"{name}() is not supported by DACS. "
                        f"Supported: {', '.join(sorted(DACS_SUPPORTED_APIS))}"
                    )
                unsupported_wrapper.__name__ = name
                return unsupported_wrapper

            # Non-callable (constants, submodules) - passthrough
            return attr

    # Change the module's class to intercept attribute access
    bf.__class__ = _PatchedBlobfileModule

    _BLOBFILE_PATCHED = True


def disable_dacs_backend():
    """
    Restore original blobfile functions.

    After calling this function, all blobfile API calls will use
    the original blobfile implementation.
    """
    global _BLOBFILE_PATCHED, _BLOBFILE_ORIGINAL_FUNCTIONS, _BLOBFILE_ORIGINAL_MODULE_CLASS

    if not _BLOBFILE_PATCHED or not _BLOBFILE_ORIGINAL_FUNCTIONS:
        return

    # Restore original functions
    bf.BlobFile = _BLOBFILE_ORIGINAL_FUNCTIONS['BlobFile']
    bf.copy = _BLOBFILE_ORIGINAL_FUNCTIONS['copy']
    bf.exists = _BLOBFILE_ORIGINAL_FUNCTIONS['exists']
    bf.listdir = _BLOBFILE_ORIGINAL_FUNCTIONS['listdir']
    bf.scandir = _BLOBFILE_ORIGINAL_FUNCTIONS['scandir']
    bf.stat = _BLOBFILE_ORIGINAL_FUNCTIONS['stat']
    bf.Error = _BLOBFILE_ORIGINAL_FUNCTIONS['Error']

    # Restore original module class
    if _BLOBFILE_ORIGINAL_MODULE_CLASS:
        bf.__class__ = _BLOBFILE_ORIGINAL_MODULE_CLASS

    _BLOBFILE_PATCHED = False
    _BLOBFILE_ORIGINAL_FUNCTIONS.clear()


def is_patched() -> bool:
    """Check if blobfile is currently patched."""
    return _BLOBFILE_PATCHED


# Auto-enable if environment variable is set
if os.environ.get("BLOBFILE_USE_DACS", "0") == "1":
    enable_dacs_backend()
