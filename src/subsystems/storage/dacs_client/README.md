# DACS Client Library

This Python library provides interfaces to perform file operations on Azure blobs with caching mechanisms. It includes async operations, a blobfile-compatible synchronous adapter (`DACSBlobfileAdapter`), and a boostedblob-compatible async adapter (`dacs_boostedblob_adapter`).

## Contents

- [Installation](#installation)
- [Authentication](#authentication)
- [DACS Cache Client API](#dacs-client-api)
  - [DACSCacheClient (async)](#dacscacheclient-async)
  - [DACSBlobfileAdapter (blobfile-compatible, sync)](#dacsblobfileadapter-blobfile-compatible-sync)
  - [DACSBoostedBlobAdapter (boostedblob-compatible, async)](#dacsboostedblobadapter-boostedblob-compatible-async)
  - [Blobfile Integration (monkey-patching)](#blobfile-integration-monkey-patching)
  - [Utility API](#utility-api)
    - [BoundedBufferManager](#boundedbuffermanager)
  - [Exception Hierarchy](#exception-hierarchy)
- [Sample Code](#sample-code)
- [FAQs](#faqs)
- [For Dev Operations](#for-dev-operations)

---

## Installation

Build the DACS Cache Client wheel from this repository or obtain one from your
build artifacts. No public package index is currently documented for this
component.

The package is built as `manylinux` wheels for both `x86_64` (amd64) and `aarch64` (arm64). The wheels are repaired with [`auditwheel`](https://github.com/pypa/auditwheel) so that all required native shared libraries (e.g. `libcurl`, `libssl`, `libjsoncpp`, `libuuid`, etc.) are bundled inside the wheel. `pip` automatically selects the correct wheel for your platform.

Wheel filenames look like:

```
dacs_client-<version>-py3-none-manylinux_X_Y_x86_64.whl
dacs_client-<version>-py3-none-manylinux_X_Y_aarch64.whl
```

The exact `manylinux_X_Y` tag depends on the glibc version of the build image; `pip` will pick the most compatible wheel.

### Install the DACS Cache Client

Install the wheel selected for your platform:

```bash
python3 -m pip install /path/to/dacs_client-<version>-*.whl
```

---

## Authentication

The DACS Cache Client supports four authentication modes, resolved automatically at initialization:

| Mode | Environment Variable(s) | Description |
| --- | --- | --- |
| **SAS Token** | `USE_SAS_TOKEN=true` and `STORAGE_INTCPT_SAS_TOKEN` | For development/testing. Set both env vars. |
| **Access Token** | `SI_azBlobAccessToken` | Provide a pre-fetched Azure access token. |
| **Managed Identity** | `AZURE_CLIENT_ID`, `MSI_ENDPOINT` and `MSI_SECRET` | For production. Falls back to IMDS endpoint if MSI vars are not set. |
| **Azure Identity SDK**  (default)| Fetch token using Defaultazurecredential in the Azure Identity SDK | Default auth mechanism if none of the above env vars are set

> **Note:** No storage account name is needed — dynamic accounts and containers are used automatically.

---

## DACS Cache Client API

### DACSCacheClient

DACS Cache Client offer both sync and async version of various operations. The sync versions have _sync suffix.

```python
from py_dacs_client.dacs_client import DACSCacheClient
```

#### Constructor

```python
DACSCacheClient(executor=None, write_mode=None)
```

| Parameter | Type | Default | Description |
| --- | --- | --- | --- |
| `executor` | `concurrent.futures.Executor` | `None` | Thread pool executor. If `None`, a `ThreadPoolExecutor` is created with `max(4, min(cpu_count * 2, 40))` workers. |
| `write_mode` | `WriteMode` | `None` | Write mode configuration. If `None`, writes are disabled. See [Write Modes](#write-modes). |

#### Blob Operations

| Method | Description |
| --- | --- |
| `get_file_size(blob_path) -> int` | Get the size (in bytes) of a blob. |
| `get_file_stat(blob_path) -> Stat64` | Get complete file statistics (size, mtime, atime, ctime, mode, uid, gid, etc.). |
| `get_file_handle(blob_path, flags) -> DACSCacheFileHandle` | Get a managed file handle for read/write. Use `os.O_RDONLY`, `os.O_WRONLY \| os.O_CREAT \| os.O_EXCL`, etc. |
| `release_file_handle(file_handle)` | Release a managed file handle and close the underlying file. |
| `read_blob(blob_path, buffer, size, offset) -> int` | Read data from a blob into a buffer. File open/close is managed automatically. Returns bytes read. |
| `write_blob(blob_path, buffer, size) -> int` | Write data from a buffer to a new blob (uses `O_CREAT \| O_EXCL`). File open/close is managed automatically. Returns bytes written. |
| `list_dir(blob_dir_path) -> AsyncIterator[Dict]` | Async iterate over directory entries. Each entry is a dict with keys: `d_ino`, `d_off`, `d_reclen`, `d_type`, `d_name`, `d_size`, `d_atime`, `d_mtime`. |
| `shutdown()` | Close all open file handles and log final metrics (reads, writes, bytes, elapsed time). |

> **Blob path format:** `blob_path` arguments accept three formats:
> - `az://account.blob.core.windows.net/container/path` (canonical format)
> - `az://account/container/path` (blobfile/boostedblob-compatible short form)
> - `https://account.blob.core.windows.net/container/path` (HTTPS form)
>
> All formats are normalized to the long `az://` form internally.
> Python normalizes HTTPS to `az://` and preserves complete hostnames without
> checking endpoint suffixes. Both native Blob backends enforce one shared
> Blob-host policy before constructing or reusing a dynamic-account service client:
> `.blob.core.windows.net`, `.blob.core.usgovcloudapi.net`,
> `.blob.core.chinacloudapi.cn`, `.blob.core.cloudapi.de`,
> `.blob.core.microsoft.scloud`, and `.blob.core.eaglex.ic.gov`.
> `AZURE_BLOB_ENDPOINT_SUFFIX` adds an operator-approved suffix, with or without
> its leading dot. Set it before using the client; it is trusted endpoint
> configuration, not a value to take from an untrusted request.
> Host matching is case-insensitive, requires a nonempty prefix before the suffix,
> and rejects empty hostname labels, URL credentials, ports, and suffix lookalikes.
> Normalization preserves the supplied hostname and path. Short-form
> `az://account/...` still defaults to public Azure, even when a custom suffix is
> configured; use a complete endpoint for sovereign/custom clouds.
> Unknown or malformed full hostnames are rejected when the native Blob client
> is reached, not by normalization. Cache hits may not reach that validation.
> Scheme and basic path-shape checks still prevent an empty or relative-directory
> authority from escaping the interception mount.
> Endpoint recognition does not configure sovereign-cloud authentication, imply
> live qualification of each cloud, or change the independent path
> parsing/dispatch of the blobfile and boostedblob adapters.

#### DACSCacheFileHandle

A context manager for managed file handle operations. Each instance owns its own file descriptor, which is opened on enter and closed on exit.

```python
async with DACSCacheFileHandle(open_path, flags, client.executor) as fh:
    bytes_read = await fh.read(buffer, size, offset)
    bytes_written = await fh.write(buffer, size, offset)
```

Supports both sync (`open_sync`/`close_sync`/`read_sync`/`write_sync`) and async (context manager with `read`/`write`) usage. Async methods delegate to the sync implementation via `run_in_executor`.

---

### Storage-Intercept Configuration

A nonempty `STORAGE_INTERCEPT_CONFIG_FILEPATH` selects an existing caller-owned
regular file. Client initialization preserves that file and its environment
setting; the file controls all settings, including write and consistent modes.
A missing file or directory path raises `FileNotFoundError`.

Without a caller-owned configuration, the client writes its per-process
`storageIntercept.<pid>.config` file. Repeated initialization rewrites that
generated configuration so the latest client arguments apply.

### DACSBlobfileAdapter (blobfile-compatible, sync)

A synchronous, blobfile-compatible adapter that wraps `DACSCacheClient`. Use this as a drop-in replacement for blobfile in read/write scenarios. Also available as the `BlobFile` alias.

```python
from py_dacs_client.dacs_blobfile_adapter import DACSBlobfileAdapter
# or use the alias:
from py_dacs_client.dacs_blobfile_adapter import BlobFile
```

#### Supported Operations

| Function / Method | Description |
| --- | --- |
| `DACSBlobfileAdapter(path, mode, ...)` | Open a file. Modes: `"r"`, `"rb"`, `"w"`, `"wb"`. Append mode is **not** supported. |
| `.read(size, buffer)` | Read data. Returns `bytes`/`str` or `memoryview` if a user buffer is provided. |
| `.write(data, size, buffer)` | Write data from `str`/`bytes` or a user-provided buffer. |
| `.seek(offset, whence)` | Seek to a position. Supported for **read-mode remote (`az://`) paths** only. `SEEK_SET`, `SEEK_CUR`, and `SEEK_END` are all supported. |
| `.tell() -> int` | Return the current file position. Supported for **remote (`az://`) paths** only. |
| `.seekable() -> bool` | Returns `True` for read-mode remote paths, `False` otherwise. |
| `.close()` | Close the file and release internally-allocated write buffers. |
| `copy(src, dst, overwrite, return_md5)` | Copy a file between local and remote paths. See [Copy Operations](#copy-operations). |
| `exists(path) -> bool` | Check if a blob exists. |
| `stat(path) -> Stat` | Get file stats (size, mtime, ctime, plus extended POSIX fields). |
| `listdir(path) -> Iterator[str]` | List filenames in a directory. |
| `scandir(path) -> Iterator[DirEntry]` | List directory entries with stat info. |
| `configure(**kwargs)` | Configure global settings. Supports `log_callback`; unsupported options raise `OperationNotSupportedError`. |
| `shutdown()` | Shutdown the client singleton and clean up resources. |

#### Copy Operations

The `copy()` function supports file copying between local and remote paths:

```python
from py_dacs_client.dacs_blobfile_adapter import copy

copy(src, dst, overwrite=False, return_md5=False)
```

| Scenario | Supported | Notes |
| --- | --- | --- |
| Local to Local | Yes | Uses `shutil.copyfile`. Supports `overwrite=True`. |
| Local to Remote (`az://`) | Yes | Streams entire file through cache layer. `overwrite` is **not** supported (always creates new). |
| Remote (`az://`) to Local | Yes | Chunked reads (256 MB chunks). Supports `overwrite=True`. |
| Remote to Remote | **No** | Raises `OperationNotSupportedError`. |

- Writes are **atomic** — a temp file is used and moved to the final destination on success.
- If `return_md5=True`, the MD5 hash of the copied data is returned.
- Raises `FileExistsError` if destination exists and `overwrite=False`.
- Raises `IsADirectoryError` if source or destination is a directory.

#### Unsupported Operations

The following blobfile operations raise `OperationNotSupportedError`: `glob`, `scanglob`, `isdir`, `makedirs`, `remove`, `rmdir`, `rmtree`, `walk`, `basename`, `dirname`, `join`, `get_url`, `md5`, `set_mtime`, `readline`, `readlines`, `writelines`, `flush`.

> **Note:** `seek()` is supported for read-mode remote paths only and raises `OperationNotSupportedError` for local paths and write-mode files. `tell()` is supported for all remote paths (both read and write mode) and raises `OperationNotSupportedError` for local paths only.

#### Write Modes

`WriteMode` is shared by `DACSCacheClient`, `DACSBlobfileAdapter`, and
`dacs_boostedblob_adapter`. Adapter-specific code can import it from the
adapter module; core `DACSCacheClient` code should import it from
`py_dacs_client`.

```python
from py_dacs_client.dacs_blobfile_adapter import WriteMode
```

| Mode | Value | Description |
| --- | --- | --- |
| `WRITE_DISABLED` | `"write_disabled"` | Read-only mode. All write operations raise an error. |
| `WRITE_CACHE_ONLY` | `"write_cache_only"` | Write to local cache only, no remote blob writes. |
| `WRITE_BLOB_ONLY` | `"write_blob_only"` | Write directly to remote blob, bypass cache. |
| `WRITE_CACHE_BLOB_ASYNC` | `"write_cache_blob_async"` | Write to cache first, async upload to blob in background. |
| `WRITE_SYNC` | `"write_sync"` | Write to cache and blob synchronously. **(Default for DACSBlobfileAdapter)** |

---

### DACSBoostedBlobAdapter (boostedblob-compatible, async)

An async, boostedblob-compatible adapter that wraps `DACSCacheClient`. Use this as a drop-in replacement for native boostedblob — it provides the same function signatures and submodule structure (`read`, `write`, `share`), but routes all operations through DACSCacheClient.

```python
import py_dacs_client.dacs_boostedblob_adapter as bbb
```

#### Supported Operations

| Function | Description |
| --- | --- |
| `await read_single(path, buffer?, size?)` | Read entire blob contents. Returns `bytes`, or `int` (bytes read) when a user buffer is provided. |
| `await read_byte_range(path, byte_range, buffer?, size?)` | Read a byte range from a blob. |
| `read_stream(path, executor?, size?)` | Async iterator yielding chunks (streaming read). |
| `read_stream_unordered(path, executor?, size?)` | Async iterator yielding chunks in unordered fashion. |
| `read_chunked(path, executor?, size?)` | Read using chunked streaming. |
| `await write_single(path, data, overwrite?)` | Write data to a new blob. |
| `await exists(path) -> bool` | Check if a blob exists. |
| `await stat(path) -> Stat` | Get blob metadata (size, mtime, ctime, etc.). |
| `await getsize(path) -> int` | Get blob size in bytes. |
| `await isdir(path) -> bool` | Check if a path is a directory. |
| `await isfile(path) -> bool` | Check if a path is a file. |
| `listdir(path)` | Async iterator listing directory contents (yields `BasePath`). |
| `scandir(path)` | Async iterator listing directory entries (yields `DirEntry`). |
| `listtree(path)` | Async iterator listing all files recursively (yields `BasePath`). |
| `scantree(path)` | Async iterator listing all files recursively (yields `DirEntry`). |
| `await shutdown()` | Clean up resources and close all file handles. |

#### Submodule Access (boostedblob API parity)

The adapter exposes synthetic submodules for API parity with native boostedblob:

```python
import py_dacs_client.dacs_boostedblob_adapter as bbb

# Read submodule
data = await bbb.read.read_single("az://account.blob.core.windows.net/container/blob.txt")
data = await bbb.read.read_byte_range("az://account.blob.core.windows.net/container/blob.txt", (0, 1024))

# Write submodule
await bbb.write.write_single("az://account.blob.core.windows.net/container/new_blob.txt", b"data")
```

#### Path Classes

| Class | Description |
| --- | --- |
| `AzurePath` | Azure blob path — fully supported. Parses `az://` and `https://*.blob.core.windows.net` URLs. |
| `BasePath` | Abstract base for path types. |
| `LocalPath`, `GooglePath` | **Not supported** — DACSCacheClient is Azure-only. |

#### Unsupported Operations

The following operations raise `OperationNotSupportedError`: `write_stream`, `write_stream_unordered`, `write_chunked`, `remove`, `rmtree`, `copyfile`, `copytree`, `sync`, `get_url`, `list_blobs`, `glob_scandir`.

#### Write Modes

The boostedblob adapter also uses the shared `WriteMode` enum:

```python
from py_dacs_client.dacs_boostedblob_adapter import WriteMode
```

---

### Blobfile Integration (monkey-patching)

The `dacs_blobfile_patcher` module allows you to transparently route all blobfile `az://` operations through the DACS Client without changing any existing blobfile code.

```python
from py_dacs_client.dacs_blobfile_patcher import enable_dacs_backend
```

#### Enabling the DACS backend

**Option A: Explicit initialization**

```python
import blobfile as bf
from py_dacs_client.dacs_blobfile_patcher import enable_dacs_backend

enable_dacs_backend()

# All bf.* calls now use DACSBlobfileAdapter for az:// paths
with bf.BlobFile("az://account.blob.core.windows.net/container/file.txt", "rb") as f:
    data = f.read()
```

**Option B: Auto-enable via environment variable**

```bash
export BLOBFILE_USE_DACS=1
```

```python
import blobfile as bf
from py_dacs_client.dacs_blobfile_patcher import enable_dacs_backend  # Auto-patches on import
```

#### How patching works

- **Supported APIs** (`BlobFile`, `copy`, `exists`, `listdir`, `scandir`, `stat`) route `az://` paths through `DACSBlobfileAdapter` and non-Azure paths through the original blobfile implementation.
- **Unsupported APIs** (e.g., `glob`, `remove`, `walk`) raise `OperationNotSupportedError` when called.
- DACSError exceptions are automatically converted to standard Python exceptions (`FileNotFoundError`, `PermissionError`, `FileExistsError`, `TimeoutError`, `IsADirectoryError`, etc.) for blobfile compatibility.

#### Disabling the patcher

```python
from py_dacs_client.dacs_blobfile_patcher import disable_dacs_backend

disable_dacs_backend()  # Restores original blobfile functions
```

#### Checking patch status

```python
from py_dacs_client.dacs_blobfile_patcher import is_patched

if is_patched():
    print("blobfile is using DACS backend")
```

---

### Utility API

```python
from py_dacs_client.dacs_utils import alloc_buffer, release_buffer, BoundedBufferManager, get_memory_limit
```

| Function / Class | Description |
| --- | --- |
| `alloc_buffer(size) -> ctypes buffer` | Allocate a raw C buffer of the specified size (via `malloc`). Raises `MemoryError` on failure. |
| `release_buffer(buffer)` | Release a previously allocated buffer (via `free`). |
| `get_memory_limit() -> int` | Get the container memory limit in bytes. Uses `DACS_MEMORY_MB` when set; otherwise derives memory as `cpu_core_count * 256 MB` (using `DACS_CPU_CORES` or `os.cpu_count()`). |
| `BoundedBufferManager(max_memory_limit)` | Allocation/free wrapper with synchronous and asynchronous APIs. Memory capping is not currently implemented. See [BoundedBufferManager](#boundedbuffermanager). |

#### BoundedBufferManager

`BoundedBufferManager` is currently a thin wrapper around `alloc_buffer()` and
`release_buffer()`. Its synchronous and asynchronous acquire methods allocate
immediately, and its release methods free the supplied buffer.

> **Important:** The current implementation does not enforce
> `max_memory_limit`, block under memory pressure, prevent out-of-memory
> conditions, or track allocated bytes. Memory capping is planned for the
> native C++ layer.

```python
from py_dacs_client.dacs_utils import BoundedBufferManager
```

##### Constructor

```python
BoundedBufferManager(max_memory_limit: int = 1024 * 1024 * 1024)
```

| Parameter | Type | Default | Description |
| --- | --- | --- | --- |
| `max_memory_limit` | `int` | `1073741824` (1 GB) | Stored for observability and future native memory-capping support. It does not currently limit allocations. |

##### Methods

| Method | Description |
| --- | --- |
| `acquire_buffer_sync(buf_size) -> ctypes.c_void_p` | Allocate a buffer immediately through `alloc_buffer()`. |
| `release_buffer_sync(buffer) -> None` | Free a buffer through `release_buffer()`. Logs a warning and returns when `buffer` is `None`. |
| `await acquire_buffer(buf_size) -> ctypes.c_void_p` | Async-compatible API that allocates a buffer immediately through `alloc_buffer()`; it does not wait for memory availability. |
| `await release_buffer(buffer) -> None` | Async-compatible API that frees a buffer through `release_buffer()`. Logs a warning and returns when `buffer` is `None`. |
| `current_memory_usage` | Always returns `0` because usage accounting is not implemented. |

##### Usage

```python
from py_dacs_client.dacs_utils import BoundedBufferManager

buffer_manager = BoundedBufferManager()

async def process_blob(client, blob_path, blob_size):
    buffer = await buffer_manager.acquire_buffer(blob_size)
    try:
        bytes_read = await client.read_blob(blob_path, buffer, blob_size, 0)
        # Process the data in buffer...
    finally:
        await buffer_manager.release_buffer(buffer)
```

This example provides paired allocation and release, but callers must control
concurrency and memory use themselves.

##### With DACSBlobfileAdapter

`DACSBlobfileAdapter` creates a shared `BoundedBufferManager` and uses it for
buffer allocation and release. The shared manager does not currently bound
aggregate memory use:

- **Reads without a user buffer:** The adapter allocates a buffer internally, copies data to Python bytes, and releases the buffer immediately.
- **Reads with a user buffer:** The user allocates via `BoundedBufferManager.acquire_buffer()` and is responsible for calling `release_buffer()` after use.
- **Writes without a user buffer:** The adapter allocates internal buffers that are automatically released on `close()`.
- **Writes with a user buffer:** The user must keep the buffer alive until `close()` and release it via `BoundedBufferManager.release_buffer()` afterward.

---

### Exception Hierarchy

All DACS-specific exceptions are importable from `py_dacs_client`:

```
DACSError (base)
├── DACSStorageError          - Storage operation failures
│   ├── DACSAuthenticationError  - Auth failures (EPERM)
│   ├── DACSPermissionError      - Access denied (EACCES)
│   ├── DACSNotFoundError        - Blob/container not found (ENOENT)
│   ├── DACSExistsError          - Resource already exists (EEXIST)
│   ├── DACSBusyError            - Resource busy/locked (EBUSY)
│   ├── DACSTimeoutError         - Operation timed out (ETIMEDOUT)
│   ├── DACSNetworkError         - Network issues (ECONNREFUSED, etc.)
│   ├── DACSQuotaError           - Quota exceeded (EDQUOT, EFBIG)
│   ├── DACSInvalidInputError    - Invalid parameters (EINVAL)
│   └── DACSServiceUnavailableError - Temporarily unavailable (EAGAIN)
└── OperationNotSupportedError   - Unsupported operations (ENOTSUP)
```
Usage:

```python
from py_dacs_client.dacs_exceptions import DACSNotFoundError, handle_native_error

try:
    size = await client.get_file_size(blob_path)
except DACSNotFoundError:
    print("Blob does not exist")
```

---

## Sample Code

### Using DACSCacheClient (async)

#### Import dependencies

```python
from py_dacs_client.dacs_client import DACSCacheClient
from py_dacs_client.dacs_utils import alloc_buffer, release_buffer
```

#### Initialize the Cache Client

Create an instance of `DACSCacheClient`. No storage account name is needed — authentication is resolved from environment variables (see [Authentication](#authentication)).

```python
client = DACSCacheClient()
```

Or with write support and a custom write mode:

```python
from py_dacs_client import WriteMode

client = DACSCacheClient(write_mode=WriteMode.WRITE_SYNC)
```

#### Get size of a blob

```python
filesize = await client.get_file_size("az://account.blob.core.windows.net/container/path/to/blob")
```

#### Get file statistics

```python
stat = await client.get_file_stat("az://account.blob.core.windows.net/container/path/to/blob")
print(f"Size: {stat.st_size}, Modified: {stat.st_mtime}")
```

#### List entries of a remote storage path

```python
async for entry in client.list_dir("az://account.blob.core.windows.net/container/dir/"):
    blob_name = entry.get("d_name")
    blob_size = entry.get("d_size")
    blob_mtime = entry.get("d_mtime")
    blob_type = entry.get("d_type")
```

#### Read blob contents

Using the async-compatible `BoundedBufferManager` allocation/free wrapper:

```python
from py_dacs_client.dacs_utils import BoundedBufferManager

buffer_manager = BoundedBufferManager()

read_size = 33554432  # 32 MB
buffer = await buffer_manager.acquire_buffer(read_size)

try:
    bytes_read = await client.read_blob(
        "az://account.blob.core.windows.net/container/path/to/blob",
        buffer, read_size, chunk_offset
    )
    # Process the data in buffer...
finally:
    await buffer_manager.release_buffer(buffer)
```

Using raw `alloc_buffer` / `release_buffer` (simple single-buffer use):

```python
read_size = 33554432  # 32 MB
buffer = alloc_buffer(read_size)

if not buffer:
    raise MemoryError("Failed to allocate buffer for reading blob")

try:
    bytes_read = await client.read_blob(
        "az://account.blob.core.windows.net/container/path/to/blob",
        buffer, read_size, chunk_offset
    )
    # Process the data in buffer...
finally:
    release_buffer(buffer)
```

#### Write blob contents

Using the async-compatible `BoundedBufferManager` allocation/free wrapper:

```python
import ctypes
from py_dacs_client.dacs_utils import BoundedBufferManager

buffer_manager = BoundedBufferManager()

write_size = len(data_bytes)
buffer = await buffer_manager.acquire_buffer(write_size)
ctypes.memmove(buffer, data_bytes, write_size)

try:
    bytes_written = await client.write_blob(
        "az://account.blob.core.windows.net/container/path/to/new_blob",
        buffer, write_size
    )
finally:
    await buffer_manager.release_buffer(buffer)
```

Using raw `alloc_buffer` / `release_buffer` (simple single-buffer use):

```python
write_size = len(data_bytes)
buffer = alloc_buffer(write_size)
import ctypes
ctypes.memmove(buffer, data_bytes, write_size)

try:
    bytes_written = await client.write_blob(
        "az://account.blob.core.windows.net/container/path/to/new_blob",
        buffer, write_size
    )
finally:
    release_buffer(buffer)
```

#### Managed file handle operations

```python
import os
from py_dacs_client.dacs_client import DACSCacheFileHandle

# Using context manager for automatic handle management
open_path = "/mnt/dacs_cache/account.blob.core.windows.net/container/blob"
async with DACSCacheFileHandle(open_path, os.O_RDONLY, client.executor) as fh:
    bytes_read = await fh.read(buffer, size, offset)
```

#### Shutdown

```python
await client.shutdown()
```

---

### Using DACSBlobfileAdapter (blobfile-compatible, sync)

#### Read a remote blob

```python
from py_dacs_client.dacs_blobfile_adapter import DACSBlobfileAdapter

with DACSBlobfileAdapter("az://account.blob.core.windows.net/container/blob", "rb") as f:
    data = f.read()
```

#### Read with a user-provided buffer (zero-copy)

Using the async-compatible `BoundedBufferManager` allocation/free wrapper:

```python
import asyncio
from py_dacs_client.dacs_blobfile_adapter import DACSBlobfileAdapter
from py_dacs_client.dacs_utils import BoundedBufferManager

buffer_manager = BoundedBufferManager()

buffer = asyncio.run(buffer_manager.acquire_buffer(1024))
try:
    with DACSBlobfileAdapter("az://account.blob.core.windows.net/container/blob", "rb") as f:
        mv = f.read(size=1024, buffer=buffer)
        # Use memoryview — buffer must stay alive
        data = bytes(mv)  # Copy if needed after release
finally:
    asyncio.run(buffer_manager.release_buffer(buffer))  # User responsible for releasing
```

Using raw `alloc_buffer` / `release_buffer`:

```python
from py_dacs_client.dacs_blobfile_adapter import DACSBlobfileAdapter
from py_dacs_client.dacs_utils import alloc_buffer, release_buffer

buffer = alloc_buffer(1024)
try:
    with DACSBlobfileAdapter("az://account.blob.core.windows.net/container/blob", "rb") as f:
        mv = f.read(size=1024, buffer=buffer)
        # Use memoryview — buffer must stay alive
        data = bytes(mv)  # Copy if needed after release
finally:
    release_buffer(buffer)  # User responsible for freeing
```

#### Write a new remote blob

```python
from py_dacs_client.dacs_blobfile_adapter import DACSBlobfileAdapter, WriteMode

with DACSBlobfileAdapter("az://account.blob.core.windows.net/container/new_blob", "wb", write_mode=WriteMode.WRITE_SYNC) as f:
    f.write(data=b"Hello, World!")
```

#### Check if a blob exists

```python
from py_dacs_client.dacs_blobfile_adapter import exists

if exists("az://account.blob.core.windows.net/container/blob"):
    print("Blob exists!")
```

#### List directory contents

```python
from py_dacs_client.dacs_blobfile_adapter import listdir

for name in listdir("az://account.blob.core.windows.net/container/dir/"):
    print(name)
```

#### Get file statistics

```python
from py_dacs_client.dacs_blobfile_adapter import stat

s = stat("az://account.blob.core.windows.net/container/blob")
print(f"Size: {s.size}, Modified: {s.mtime}")
```

#### Copy files

```python
from py_dacs_client.dacs_blobfile_adapter import copy

# Remote to local
copy("az://account.blob.core.windows.net/container/blob", "/local/path/file.bin")

# Local to remote
copy("/local/path/file.bin", "az://account.blob.core.windows.net/container/new_blob")

# With MD5 hash
md5_hash = copy("az://account/container/blob", "/local/file", return_md5=True)
print(f"MD5: {md5_hash}")
```

#### Using blobfile with DACS backend

```python
import blobfile as bf
from py_dacs_client.dacs_blobfile_patcher import enable_dacs_backend

enable_dacs_backend()

# Existing blobfile code works transparently with DACS
with bf.BlobFile("az://account.blob.core.windows.net/container/blob", "rb") as f:
    data = f.read()

if bf.exists("az://account.blob.core.windows.net/container/blob"):
    print("Blob exists!")

for name in bf.listdir("az://account.blob.core.windows.net/container/dir/"):
    print(name)
```

---

### Using DACSBoostedBlobAdapter (boostedblob-compatible, async)

#### Read a blob

```python
import asyncio
import py_dacs_client.dacs_boostedblob_adapter as bbb

async def main():
    data = await bbb.read.read_single("az://account.blob.core.windows.net/container/blob")
    print(f"Read {len(data)} bytes")

    await bbb.shutdown()

asyncio.run(main())
```

#### Read a byte range

```python
import asyncio
import py_dacs_client.dacs_boostedblob_adapter as bbb

async def main():
    data = await bbb.read.read_byte_range(
        "az://account.blob.core.windows.net/container/blob",
        byte_range=(0, 1024)
    )
    print(f"Read {len(data)} bytes")

    await bbb.shutdown()

asyncio.run(main())
```

#### Write a blob

```python
import asyncio
import py_dacs_client.dacs_boostedblob_adapter as bbb

async def main():
    await bbb.write.write_single(
        "az://account.blob.core.windows.net/container/new_blob",
        b"Hello, World!"
    )

    await bbb.shutdown()

asyncio.run(main())
```

#### List and scan directories

```python
import asyncio
import py_dacs_client.dacs_boostedblob_adapter as bbb

async def main():
    # List filenames
    async for path in bbb.listdir("az://account.blob.core.windows.net/container/dir/"):
        print(path)

    # Scan with stat info
    async for entry in bbb.scandir("az://account.blob.core.windows.net/container/dir/"):
        print(f"{entry.path}: size={entry.stat.size}")

    await bbb.shutdown()

asyncio.run(main())
```

#### Streaming read

```python
import asyncio
import py_dacs_client.dacs_boostedblob_adapter as bbb

async def main():
    async for chunk in bbb.read.read_stream("az://account.blob.core.windows.net/container/large_blob"):
        process(chunk)

    await bbb.shutdown()

asyncio.run(main())
```

#### Using AzurePath

```python
import asyncio
from py_dacs_client.dacs_boostedblob_adapter import AzurePath, stat
import py_dacs_client.dacs_boostedblob_adapter as bbb

async def main():
    path = AzurePath.from_str("az://account.blob.core.windows.net/container/blob")
    s = await stat(path)
    print(f"Size: {s.size}, Modified: {s.mtime}")

    await bbb.shutdown()

asyncio.run(main())
```

---

### Environment Variable Configuration

| Variable | Required | Description |
| --- | --- | --- |
| `USE_SAS_TOKEN` | No | Set to `"true"` to enable SAS token auth. |
| `STORAGE_INTCPT_SAS_TOKEN` | When `USE_SAS_TOKEN=true` | The SAS token for blob access. |
| `SI_azBlobAccessToken` | No | Pre-fetched Azure access token. |
| `AZURE_CLIENT_ID` | For managed identity | Azure managed identity client ID. |
| `MSI_ENDPOINT` | No | MSI endpoint URL. Defaults to IMDS. |
| `MSI_SECRET` | No | MSI secret. Defaults to `"dummy"`. |
| `CACHE_SERVER_LIST` | No | Comma-separated list of cache servers. Enables caching when set with `CACHE_SERVER_PORT`. |
| `CACHE_DISCOVERY_URL` | No | Cache server discovery endpoint URL. Enables dynamic cache server discovery when set with `CACHE_SERVER_PORT`. Takes precedence over `CACHE_SERVER_LIST`. |
| `CACHE_SERVER_PORT` | No | Cache server port. Required for both static and discovery-based cache configurations. |
| `BLOBFILE_USE_DACS` | No | Set to `"1"` to auto-enable blobfile monkey-patching on import. See [Blobfile Integration](#blobfile-integration-monkey-patching). |
| `DACS_MEMORY_MB` | No | Container memory limit in MB returned by `get_memory_limit()`. If unset/invalid, the value defaults to `cpu_core_count * 256 MB` (e.g., 8 cores -> 2 GB, 128 cores -> 32 GB). `BoundedBufferManager` does not currently enforce this value. |
| `DACS_PY_CACHE_CLIENT_THREADPOOL_SIZE` | No | Thread pool size for parallel downloads. Defaults to `min(cpu_count * 4, 48)`. |
| `DACS_PY_CACHE_CLIENT_CHUNK_SIZE` | No | Chunk size in bytes for chunked blob downloads. Defaults to `268435456` (256 MB). |
| `DACS_DATA_LOG_LEVEL` | No | Logging level (`DEBUG`, `INFO`, `WARNING`, etc.). Defaults to `INFO`. |
| `DACS_ENABLE_IB_ZERO_COPY` | No | Set to `true` to enable the zero-copy InfiniBand upload path (registers the caller's buffer directly instead of copying it into the send buffer). Disabled by default; falls back to the copy-based path when unset or not `true`. |

---

## FAQs

### Missing native library at runtime

If you see:

```
FATAL: Required library 'libStorageDirect.so' not found!
```

Ensure `libStorageDirect.so` is present in the same directory as the `py_dacs_client` package. This shared library provides the native storage intercept layer.

---

# For Dev Operations

## Throughput Benchmarking

For Kubernetes throughput benchmarking, use the benchmark scripts in
`aks_stress_test/`:

```bash
cd aks_stress_test
./run_benchmark.sh
```

The benchmark deploys DACS clients as a DaemonSet and measures read/write
throughput. Results include per-chunk latency percentiles and overall
throughput metrics.

## To Build

The wheel is built by the helper script
`.pipelines/scripts/build/build-python-package.sh`. It can also be run locally.

### Prerequisites

1. Build the native components first so that `libStorageDirect.so` exists at one of the paths `setup.py` looks for:
   - `bin/Debug/<arch>/native-storage/storage/libStorageDirect.so`
   - `bin/Release/<arch>/native-storage/storage/libStorageDirect.so`

   ```bash
   make build BUILD_TYPE=Release
   ```

2. Install the Python build dependencies. On modern distros (Ubuntu 23.04+, Debian 12+) Python is "externally managed" per PEP 668, so do this inside a virtualenv:

   ```bash
   python3 -m venv .venv
   source .venv/bin/activate
   python3 -m pip install --require-hashes \
     -r .pipelines/requirements/python-build.txt
   ```

   `auditwheel` is pinned to 6.7.0 because
   [6.8.0 can produce invalid `DT_NEEDED` and `RPATH` entries](https://github.com/pypa/auditwheel/issues/722),
   causing repaired wheels to fail when imported.

Regenerate all Python lock files with Python 3.10 and the locked compiler
environment:

```bash
python3.10 -m venv .venv-requirements
source .venv-requirements/bin/activate
python -m pip install --require-hashes \
  -r .pipelines/requirements/compile.txt
./.pipelines/scripts/update-python-requirements.sh
```

### Build via the helper script (recommended)

```bash
export BUILD_ARCHITECTURE=amd64 # 'amd64' or 'arm64' (auto-detected if unset)

./.pipelines/scripts/build/build-python-package.sh
ls -al src/subsystems/storage/dacs_client/dist/
```

The package version is derived from Git according to
[VERSIONING.md](../../../../VERSIONING.md).

The script:

1. Substitutes the derived version in `py_dacs_client/__init__.py`; `pyproject.toml` keeps the fixed `dacs-client` distribution name.
2. Runs `python -m build`, producing a wheel named `dacs_client-<version>-py3-none-linux_{x86_64,aarch64}.whl`.
3. Restores `py_dacs_client/__init__.py`.
4. Runs `auditwheel repair` on the produced wheel. This bundles all required native shared libraries into the wheel and re-tags it as `manylinux_X_Y_{x86_64,aarch64}`.
5. Deletes the unrepaired `linux_*` wheel so only the `manylinux` wheel remains.

The final artifact in `dist/` looks like (the `manylinux_X_Y` tag is set by `auditwheel` based on the build image's glibc, e.g. `manylinux_2_31` on Ubuntu 20.04):

```
dacs_client-0.1.0-py3-none-manylinux_X_Y_x86_64.whl
```

### Build manually

Use the helper script above so the wheel receives the Git-derived version and
the staged package metadata is restored even when the build fails.

## To Publish

The release pipeline produces repaired `manylinux` wheels for both `amd64` and
`arm64`. Public package publication is configured separately from the source
tree; do not commit repository credentials or feed URLs.

## To Test

```
Set authentication (choose one method):

For SAS Token (Development):
export STORAGE_INTCPT_SAS_TOKEN="your-sas-token"

For Managed Identity (Production):
export AZURE_CLIENT_ID="your-client-id"
# Optional: MSI_ENDPOINT and MSI_SECRET default to IMDS values
export MSI_ENDPOINT="http://169.254.169.254/metadata/identity/oauth2/token"
export MSI_SECRET="dummy"

Validate installation:
python3 -c "from py_dacs_client import dacs_client; print(dacs_client.help())"
```
