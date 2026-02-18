#!/usr/bin/env python3
# -------------------------------------------------------
# DACS Blobfile Exception Handler
#
# Converts DACSErrors to standard Python OSError subclasses
# for blobfile compatibility.
# -------------------------------------------------------

import errno
import os

# Errno to exception class mapping
_ERRNO_TO_EXCEPTION = {
    errno.ENOENT: FileNotFoundError,
    errno.EACCES: PermissionError,
    errno.EPERM: PermissionError,
    errno.EISDIR: IsADirectoryError,
    errno.ENOTDIR: NotADirectoryError,
    errno.EEXIST: FileExistsError,
    errno.ETIMEDOUT: TimeoutError,
    errno.ENOTEMPTY: OSError,
    errno.ENAMETOOLONG: OSError,
    errno.EROFS: PermissionError,
    # Network/service errors → OSError
    errno.ECONNREFUSED: OSError,
    errno.ENETUNREACH: OSError,
    errno.EHOSTUNREACH: OSError,
    errno.ECONNRESET: OSError,
    errno.EAGAIN: OSError,
    errno.EBUSY: OSError,
    errno.EDQUOT: OSError,
    errno.EFBIG: OSError,
    errno.EINVAL: OSError,
    errno.ENOTSUP: OSError,
    errno.EIO: OSError,
}


def _copy_diagnostic_attributes(src_exc: Exception, dst_exc: Exception) -> None:
    """Copy diagnostic attributes from source to destination exception."""
    for attr in ['operation', 'path', 'error_code', 'errno_code', 'request_id', 'message', 'cause']:
        if hasattr(src_exc, attr):
            setattr(dst_exc, attr, getattr(src_exc, attr))


def normalize_exception(exc: Exception, original_path: str = None) -> Exception:
    """
    Normalize exceptions to standard Python types.
    
    Converts DACSError to standard Python OSError subclasses.
    Standard exceptions pass through unchanged.
    
    Args:
        exc: The exception to normalize (may be DACSError or standard exception)
        original_path: Optional original path to use instead of the cached path in exception
    
    Returns:
        Standard Python exception with diagnostic attributes preserved
    """
    from py_dacs_client.dacs_exceptions import DACSError
    
    # Only convert DACSError instances; everything else passes through unchanged
    if not isinstance(exc, DACSError):
        if original_path is not None and isinstance(exc, OSError) and getattr(exc, "errno", None) is not None:
            return exc.__class__(exc.errno, exc.strerror, original_path)
        return exc
    
    # Get errno_code and map to exception class
    errno_code = getattr(exc, 'errno_code', None)
    exc_class = _ERRNO_TO_EXCEPTION.get(errno_code, OSError)
    
    # Preserve the original message from DACSError for debugging context.
    # Falls back to os.strerror if no message is available.
    error_msg = getattr(exc, 'message', None) or (os.strerror(errno_code) if errno_code else str(exc))
    
    # Use original_path if provided, otherwise use path from exception
    path_to_use = original_path if original_path is not None else getattr(exc, 'path', None)
    
    if errno_code is not None:
        new_exc = exc_class(errno_code, error_msg, path_to_use)
    else:
        new_exc = exc_class(error_msg)
    
    # Preserve diagnostic attributes
    _copy_diagnostic_attributes(exc, new_exc)
    return new_exc


def convert_and_raise(exc: Exception, original_path: str = None) -> None:
    """
    Normalize exception to standard type and raise, avoiding self-reference cycles.
    
    If exception is not a DACSError, re-raise unchanged.
    Otherwise, raise normalized exception without showing conversion in traceback.
    
    Args:
        exc: The exception to convert and raise
        original_path: Optional original path (e.g., az:// path) to use instead of cached path
    """
    normalized_exc = normalize_exception(exc, original_path=original_path)
    # If normalize_exception returned the same instance, it means exception
    # wasn't DACSError and passed through unchanged - re-raise as-is
    if normalized_exc is exc:
        raise
    raise normalized_exc from None
