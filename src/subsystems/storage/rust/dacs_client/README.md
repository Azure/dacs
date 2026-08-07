# DACS Rust Client

Rust client for the DACS storage-intercept layer. It loads
`libStorageDirect.so` at runtime and exposes file operations on Azure blobs
through the DACS cache layer.

## Installation

Use the client from this repository:

```toml
[dependencies]
dacs-rust-client = { path = "src/subsystems/storage/rust/dacs_client" }
```

```rust
use dacs_client::Client;

let client = Client::new()?;

let stat = client.file_stat("az://account/container/file.bin")?;

let mut buffer = vec![0u8; stat.size as usize];
let bytes_read = client.read_blob("az://account/container/file.bin", &mut buffer, 0)?;

let entries = client.list_dir("az://account/container/dir")?;
```

Supported path forms match the Python client:

- `az://account.blob.core.windows.net/container/path`
- `az://account/container/path`
- `https://account.blob.core.windows.net/container/path`

## API Reference

### Client

| Method | Description |
|---|---|
| `Client::new()` | Create client with default config (read-only, auth from env) |
| `Client::with_config(config)` | Create client with custom `ClientConfig` |
| `client.file_size(blob_path)` | Get size in bytes |
| `client.file_stat(blob_path)` | Get file metadata (size, mode, mtime, atime, ctime) |
| `client.read_blob(blob_path, buffer, offset)` | Read into buffer at offset (retries on partial reads) |
| `client.write_blob(blob_path, buffer, overwrite)` | Write buffer to blob. `overwrite=false`: fails if exists (O_EXCL). `overwrite=true`: truncates (O_TRUNC, requires consistent mode) |
| `client.list_dir(blob_dir_path)` | List directory entries |
| `client.open(blob_path, flags)` | Open file handle for read/write operations |
| `client.config_path()` | Path to the generated config file |

### FileHandle

| Method | Description |
|---|---|
| `handle.read(buffer, offset)` | Read bytes at offset (pread) |
| `handle.write(buffer, offset)` | Write bytes at offset (pwrite) |
| `handle.close()` | Close explicitly (also called on drop) |

### ClientConfig

| Method | Description |
|---|---|
| `ClientConfig::new()` | Default config (read-only, `/mnt/dacs_cache`) |
| `.with_write_mode(WriteMode)` | Enable writes (`WriteSync`, `WriteBlobOnly`, etc.) |
| `.with_consistent_mode(true)` | Enable overwrite support |
| `.with_intercept_path(path)` | Custom mount path |
| `.with_config_dir(path)` | Custom config file directory |

### WriteMode

| Variant | Description |
|---|---|
| `WriteDisabled` | Read-only (default) |
| `WriteCacheOnly` | Write to cache only |
| `WriteBlobOnly` | Write to blob only |
| `WriteCacheBlobAsync` | Write cache, async upload to blob |
| `WriteSync` | Write to cache and blob synchronously |

## Native Library Loading

### Embedded native libraries

The crate can embed `libStorageDirect.so` for amd64 or arm64 when native
libraries are staged under `native/<target>`. At runtime, the library is
extracted to `~/.cache/dacs-client/<version>/` and loaded with `dlopen`.

### For developers (working in the repo)

Set `DACS_LIB_PATH` to point at the `.so` built by `make build`:

```bash
export DACS_LIB_PATH=$(pwd)/bin/Debug/amd64/native-storage/storage/libStorageDirect.so
```

If `DACS_LIB_PATH` is set but the file is missing, the client fails
immediately. If it is not set and no embedded payload exists, the client
returns a `NotFound` error.

### How `.so` embedding works

Stage the required architecture's `.so` files into the crate:

```
native/linux-x86_64/libStorageDirect.so
native/linux-aarch64/libStorageDirect.so
```

`build.rs` generates `include_bytes!()` entries for the matching platform's `.so` files, so they're compiled into the consumer's binary. `loader.rs` extracts them to disk at runtime (dlopen requires a file path).

## Runtime Configuration

`Client::new()` writes the same Storage Intercept config file pattern as the Python client and sets `STORAGE_INTERCEPT_CONFIG_FILEPATH` for the native library.

Authentication is resolved from environment in this order:

1. `USE_SAS_TOKEN=true` and `STORAGE_INTCPT_SAS_TOKEN`
2. `SI_azBlobAccessToken`
3. `AZURE_CLIENT_ID`, `MSI_ENDPOINT`, and `MSI_SECRET`
4. Azure Identity SDK mode in the native layer

Cache configuration uses either `CACHE_DISCOVERY_URL` + `CACHE_SERVER_PORT`, or `CACHE_SERVER_LIST` + `CACHE_SERVER_PORT`.

## For Dev Operations

### Prerequisites

Build the native components first so that `libStorageDirect.so` exists:

```bash
make build
```

### Running tests

```bash
# Unit tests (no native library or auth needed)
make test-rust-client

# Integration tests require a built .so, authentication, and test data.
export USE_SAS_TOKEN=true
export STORAGE_INTCPT_SAS_TOKEN='<your-sas-token>'
export DACS_LIB_PATH="$(pwd)/bin/Debug/amd64/native-storage/storage/libStorageDirect.so"
export DACS_TEST_BLOB_PATH='az://account/container/test-data'
make test-rust-client-integration
```

### Building the package locally

Build the native components and then create a crate containing the matching
architecture's `libStorageDirect.so`:

```bash
make build BUILD_TYPE=Release
make rust-package BUILD_TYPE=Release
```

The crate is written to `target/package/` under this directory. GitHub Actions
builds and tests Rust with the C++ and Go components, then uploads
`dacs-rust-crate-amd64` and `dacs-rust-crate-arm64` workflow artifacts. The
workflow does not publish the crate to crates.io or any other registry.