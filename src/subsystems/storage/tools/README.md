# GetCacheKeyAndServerIndex Tool

This tool generates cache keys and server indices for Azure Storage blob chunks, matching the behavior of the DACS storage system's caching mechanism.

## Architecture

The tool is structured with proper separation of concerns:
- **GetCacheKeyAndServerIndex.h** - Function declarations for testable interfaces
- **GetCacheKeyAndServerIndex.cpp** - Main implementation that reuses existing DACS utilities
- **GetCacheKeyAndServerIndexTest.cpp** - Comprehensive unit tests testing the actual implementation
- **CMakeLists.txt** - Build configuration that creates both a static library and executable

The implementation reuses existing DACS functionality:
- `CacheUtils::GetSHA256Hex()` and `CacheUtils::GetServerIndex()` - from existing cache utilities
- `StorageInterceptConstants::CacheKeyAccountNameOverride` - for consistent account name handling

## Usage

```
GetCacheKeyAndServerIndex <account_name> <container_name> <blob_path> <offset> <chunk_size> <ignore_account_name> <num_servers>
```

## Parameters

- `account_name`: Storage account name
- `container_name`: Container name  
- `blob_path`: Path to the blob within the container
- `offset`: File offset (bytes)
- `chunk_size`: Chunk size (bytes)
- `ignore_account_name`: Whether to ignore account name in cache key (true/false)
- `num_servers`: Number of cache servers

## Example

```bash
GetCacheKeyAndServerIndex myaccount mycontainer path/to/file.txt 0 4194304 false 4
```

This will output:
- The cache file path used for server index calculation
- The SHA256 hash-based cache key
- The calculated server index

## Build Instructions

To build this tool, ensure you have:
1. CMake 3.10 or later
2. A C++17 compatible compiler
3. OpenSSL development libraries

From the tools directory:
```bash
mkdir build
cd build
cmake ..
make
```

## Implementation Details

The tool replicates the following logic from the DACS storage system:

1. **Cache Key Generation** (based on `RemoteCacheClient::GetCacheKey`):
   - Creates a string from cache prefix, container name, blob path, offset, and chunk size
   - Computes SHA256 hash of this string

2. **Server Index Calculation** (based on `CacheUtils::GetServerIndex`):
   - Uses hash of the cache file path to determine server index
   - Distributes load across available cache servers

3. **Cache File Path Generation** (based on `StreamingClient::GetCacheFilePath`):
   - Handles account name inclusion/exclusion based on configuration
   - Used as input for server index calculation