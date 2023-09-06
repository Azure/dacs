# Cache-client rust integration test

The rust integration test is built during cache-client library build.
The test is run during storage pipeline tests.

## A manual build `cargo build` requires these env vars to be set:
- LIBRARY_PATH: Directory of cache client SO
- CACHE_CLIENT_LIB_DIR: Directory of cache client SO
- CACHE_CLIENT_HEADER_FILE: Path to cache client header file

## A manual run `cargo run` requires these env vars to be set:
- LD_LIBRARY_PATH: Directory of cache client SO
