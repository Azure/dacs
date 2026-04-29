# DACS Client Library

This Python library provides interfaces to perform file operations on Azure blobs with caching mechanisms. It includes async operations, a blobfile-compatible synchronous adapter (`DACSBlobfileAdapter`), and a boostedblob-compatible async adapter (`dacs_boostedblob_adapter`).

**Wiki Link:** internal-reference-removed

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

Install the latest DACS Cache Client package from the official feed.

**Feed:** internal-reference-removed

**Package:** internal-reference-removed

The package includes platform-specific wheels for both amd64 and arm64. `pip` automatically selects the correct wheel for your platform.

### Prerequisites to connect to the Feeds

Ensure to install the latest version of the Azure Artifacts keyring which depends on .NET. Given below are the steps required for Ubuntu 20.04 linux distribution.

Before you install .NET, run the following commands to add the Microsoft package signing key to your list of trusted keys and add the package repository.

```bash
wget https://packages.microsoft.com/config/ubuntu/20.04/packages-microsoft-prod.deb -O packages-microsoft-prod.deb
sudo dpkg -i packages-microsoft-prod.deb
rm packages-microsoft-prod.deb
```

Install the .NET SDK:

```bash
sudo apt-get update && sudo apt-get install -y dotnet-sdk-8.0
```

Install latest version of the Azure Artifacts keyring:

```bash
python -m pip install --upgrade pip
pip install keyring artifacts-keyring
```

### Install the DACS Cache Client

Once the prerequisites are met to connect to the feed, install the package:

```bash
pip install -i internal-reference-removed dacs-client-official
```

To install a specific version:

```bash
pip install -i internal-reference-removed dacs-client-official==0.9.0.20260218.4
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
> - `az://account.blob.core.windows.net/container/path` (long form — DACS internal format)
> - `az://account/container/path` (short form — blobfile/boostedblob compatible)
> - `https://account.blob.core.windows.net/container/path` (https form — blobfile compatible)
>
> All formats are normalized to the long `az://` form internally.

#### DACSCacheFileHandle

A context manager for managed file handle operations. Each instance owns its own file descriptor, which is opened on enter and closed on exit.

```python
async with DACSCacheFileHandle(open_path, flags, client.executor) as fh:
    bytes_read = await fh.read(buffer, size, offset)
    bytes_written = await fh.write(buffer, size, offset)
```

Supports both sync (`open_sync`/`close_sync`/`read_sync`/`write_sync`) and async (context manager with `read`/`write`) usage. Async methods delegate to the sync implementation via `run_in_executor`.

---

### DACSBlobfileAdapter (blobfile-compatible, sync)

A synchronous, blobfile-compatible adapter that wraps `DACSCacheClient`. Use this as a drop-in replacement for blobfile in read/write scenarios. Also available as the `BlobFile` alias.

```python
from py_dacs_client.dacs_blobfile_adapter import DACSBlobfileAdapter, WriteMode
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

The same `WriteMode` enum is available:

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
| `BoundedBufferManager(max_memory_limit)` | Async-safe buffer pool that enforces a total memory usage cap. See [BoundedBufferManager](#boundedbuffermanager). |

> **Note:** For production workloads, prefer `BoundedBufferManager` over raw `alloc_buffer`/`release_buffer` to prevent unbounded memory growth when processing many blobs concurrently.

#### BoundedBufferManager

An async-safe buffer pool that enforces a total memory usage cap across all concurrent operations. It prevents out-of-memory conditions by blocking `acquire_buffer()` calls when the memory limit would be exceeded, and automatically unblocking them when buffers are released.

Internally, `BoundedBufferManager` uses `asyncio.Condition` (not a bare `Event`) so waiters re-check allocation predicates under lock. This avoids lost-wakeup races under high concurrency.

```python
from py_dacs_client.dacs_utils import BoundedBufferManager, get_memory_limit
```

##### Constructor

```python
BoundedBufferManager(max_memory_limit: int = 1024 * 1024 * 1024)
```

| Parameter | Type | Default | Description |
| --- | --- | --- | --- |
| `max_memory_limit` | `int` | `1073741824` (1 GB) | Maximum total bytes that can be allocated across all buffers. A common pattern is to use 80% of the container memory limit via `get_memory_limit()`. |

`max_memory_limit` must be greater than `0`; otherwise `ValueError` is raised.

##### Methods

| Method | Description |
| --- | --- |
| `await acquire_buffer(buf_size) -> ctypes.c_void_p` | Allocate a buffer of the requested size. Blocks (async) if the allocation would exceed `max_memory_limit`, resuming automatically when other buffers are released. If `buf_size` exceeds `max_memory_limit`, the allocation proceeds anyway to avoid deadlock. Raises `MemoryError` on `malloc` failure. |
| `await release_buffer(buffer) -> None` | Release a previously acquired buffer back to the pool (frees the underlying C memory via `free`). Signals waiting `acquire_buffer()` calls that memory may be available. Logs a warning and skips if `buffer` is `None`. If the buffer size cannot be determined, memory is still freed and accounting decrement is skipped with a warning. |

##### Memory Pressure Logging

`BoundedBufferManager` logs at `INFO` level when memory usage exceeds 90% of the limit, and at `DEBUG` level otherwise. This helps identify memory pressure without excessive log noise.

##### Test Coverage

Unit tests in `py_dacs_client/tests/test_bounded_buffer_manager.py` validate:
- memory-pressure blocking/unblocking
- multiple waiting acquirers under constrained memory
- allocation-failure accounting correctness
- `None`/unknown buffer release edge cases
- constructor validation for invalid memory limits

##### Standalone Usage (async)

```python
import asyncio
from py_dacs_client.dacs_utils import BoundedBufferManager, get_memory_limit

memory_limit = get_memory_limit()
buffer_manager = BoundedBufferManager(max_memory_limit=int(memory_limit * 0.8))

async def process_blob(client, blob_path, blob_size):
    buffer = await buffer_manager.acquire_buffer(blob_size)
    try:
        bytes_read = await client.read_blob(blob_path, buffer, blob_size, 0)
        # Process the data in buffer...
    finally:
        await buffer_manager.release_buffer(buffer)

# Multiple concurrent reads are automatically bounded
async def main():
    tasks = [process_blob(client, path, size) for path, size in blob_list]
    await asyncio.gather(*tasks)
```

##### With DACSBlobfileAdapter (automatic)

When using `DACSBlobfileAdapter`, a singleton `BoundedBufferManager` (80% of container memory) is created automatically — no manual setup is needed. Internal read and write buffers are acquired and released through this shared pool:

- **Reads without a user buffer:** The adapter acquires a buffer internally, copies data to Python bytes, and releases the buffer immediately.
- **Reads with a user buffer:** The user allocates via `BoundedBufferManager.acquire_buffer()` and is responsible for calling `release_buffer()` after use.
- **Writes without a user buffer:** The adapter acquires internal buffers that are automatically released on `close()`.
- **Writes with a user buffer:** The user must keep the buffer alive until `close()` and release it via `BoundedBufferManager.release_buffer()` afterward.

```python
import asyncio
from py_dacs_client.dacs_blobfile_adapter import DACSBlobfileAdapter
from py_dacs_client.dacs_utils import BoundedBufferManager

# For user-managed buffer reads, create or share a BoundedBufferManager
buffer_manager = BoundedBufferManager(max_memory_limit=1024 * 1024 * 1024)

async def read_blob_with_buffer():
    buffer = await buffer_manager.acquire_buffer(4096)
    try:
        with DACSBlobfileAdapter("az://account.blob.core.windows.net/container/blob", "rb") as f:
            mv = f.read(size=4096, buffer=buffer)
            data = bytes(mv)
    finally:
        await buffer_manager.release_buffer(buffer)

asyncio.run(read_blob_with_buffer())
```

##### Concurrent Blob Downloads with Bounded Memory

```python
import asyncio
from py_dacs_client.dacs_client import DACSCacheClient
from py_dacs_client.dacs_utils import BoundedBufferManager, get_memory_limit

memory_limit = get_memory_limit()
buffer_manager = BoundedBufferManager(max_memory_limit=int(memory_limit * 0.8))
client = DACSCacheClient()

async def download_blob(blob_path, blob_size):
    buffer = await buffer_manager.acquire_buffer(blob_size)
    try:
        await client.read_blob(blob_path, buffer, blob_size, 0)
        # Process buffer contents...
    finally:
        await buffer_manager.release_buffer(buffer)

async def main():
    blob_list = [(f"az://account.blob.core.windows.net/container/blob_{i}", 32 * 1024 * 1024) for i in range(100)]
    tasks = [download_blob(path, size) for path, size in blob_list]
    await asyncio.gather(*tasks)  # Memory stays within 80% of container limit
    await client.shutdown()

asyncio.run(main())
```

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
from py_dacs_client.dacs_blobfile_adapter import WriteMode

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

Using `BoundedBufferManager` (recommended for concurrent workloads):

```python
from py_dacs_client.dacs_utils import BoundedBufferManager, get_memory_limit

memory_limit = get_memory_limit()
buffer_manager = BoundedBufferManager(max_memory_limit=int(memory_limit * 0.8))

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

Using `BoundedBufferManager` (recommended for concurrent workloads):

```python
import ctypes
from py_dacs_client.dacs_utils import BoundedBufferManager, get_memory_limit

memory_limit = get_memory_limit()
buffer_manager = BoundedBufferManager(max_memory_limit=int(memory_limit * 0.8))

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

Using `BoundedBufferManager` (recommended — enforces memory limits):

```python
import asyncio
from py_dacs_client.dacs_blobfile_adapter import DACSBlobfileAdapter
from py_dacs_client.dacs_utils import BoundedBufferManager

buffer_manager = BoundedBufferManager(max_memory_limit=1024 * 1024 * 1024)  # 1 GB limit

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
| `DACS_MEMORY_MB` | No | Container memory limit in MB. Used by `get_memory_limit()` and `BoundedBufferManager` to derive buffer pool size. If unset/invalid, memory defaults to `cpu_core_count * 256 MB` (e.g., 8 cores → 2 GB, 128 cores → 32 GB). |
| `DACS_PY_CACHE_CLIENT_THREADPOOL_SIZE` | No | Thread pool size for parallel downloads. Defaults to `min(cpu_count * 4, 48)`. |
| `DACS_PY_CACHE_CLIENT_CHUNK_SIZE` | No | Chunk size in bytes for chunked blob downloads. Defaults to `268435456` (256 MB). |
| `DACS_DATA_LOG_LEVEL` | No | Logging level (`DEBUG`, `INFO`, `WARNING`, etc.). Defaults to `INFO`. |

---

## FAQs

### Missing shared library dependencies on Ubuntu 20.04

If Docker images give errors like:

```
OSError: libjsoncpp.so.1: cannot open shared object file: No such file or directory
```

Run the following before installing the package:

```bash
sudo apt-get update && \
sudo apt-get install -y --no-install-recommends -o Dpkg::Options::="--force-confdef" libssl-dev libcurl4-openssl-dev curl libjsoncpp-dev uuid-dev ibverbs-providers && \
ln -sf /usr/lib/x86_64-linux-gnu/libjsoncpp.so.1.9.5 /usr/lib/x86_64-linux-gnu/libjsoncpp.so.1 && \
wget https://nz2.archive.ubuntu.com/ubuntu/pool/main/o/openssl/libssl1.1_1.1.1f-1ubuntu2.24_amd64.deb && \
dpkg -i libssl1.1_1.1.1f-1ubuntu2.24_amd64.deb || sudo apt-get install -f -y && \
python3 -m pip install --no-cache-dir requests
```

### Missing native library at runtime

If you see:

```
FATAL: Required library 'libStorageDirect.so' not found!
```

Ensure `libStorageDirect.so` is present in the same directory as the `py_dacs_client` package. This shared library provides the native storage intercept layer.

---

# For Dev Operations

## Throughput Benchmarking

For AKS throughput benchmarking, use the benchmark scripts in `aks_stress_test/`:

```bash
cd aks_stress_test
./run_benchmark.sh
```

The benchmark deploys dacs clients as a DaemonSet across your AKS cluster and measures read/write throughput under various conditions. Results include per-chunk latency percentiles (p50, p90, p99) and overall throughput metrics.

## To Build

```bash
docker run -it --entrypoint /bin/bash -v /mnt/c/CodeBase/DACS/src/subsystems/storage:/storage mcr.microsoft.com/vscode/devcontainers/python:3.11

#Install Azure CLI and Login, if below setup.py fails
#curl -sL https://aka.ms/InstallAzureCLIDeb | sudo bash
#az login

cd /storage/dacs_client
python setup.py bdist_wheel
ls -al dist/
```

## To Publish

```
To make a release with updated version, ensure to change VERSION in setup.py.
    /src/subsystems/storage/dacs_client/setup.py

This would help to generate the whl with version starting as "{distribution}-{version}.(-{build tag})?"

Configure the .pypirc file
    [distutils]
    Index-servers = DACS-Feed

    [DACS-Feed]
    Repository = internal-reference-removed
    Username = <feed-username>
    Password = <PAT>

cd /storage/dacs_client/
python3 -m pip install --upgrade twine
python3 -m twine upload --repository DACS-Feed dist/* --config ./.pypirc
```

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

Run the setup script (if needed):
sh /usr/local/lib/python3.11/site-packages/dacs_client/setup.sh

Validate installation:
root ➜ /storage/2020-12-01-preview/_generated $ python
Python 3.11.11 (main, Dec  4 2024, 20:38:25) [GCC 12.2.0] on linux
Type "help", "copyright", "credits" or "license" for more information.
>>> from py_dacs_client import dacs_client
>>> dacs_client.help()
'\n    DACS Client library exposes interfaces to perform file operations.\n    '
>>>

Run a test job:
/storage/2020-12-01-preview/_generated/dacs_client/sample $ python dacs_client_test.py
```