## Cache server

Usage: ./CacheServer --port <port> [--cacheRoot <cache folder root>] [--sizeLimitBytes <cache size in bytes - default 1TB>] \
[--ttlWindowSeconds <cache expiry ttl window size in seconds - default 60 seconds>]

### Supported environment variables:

Boolean settings accept `1` or case-insensitive `true` to enable the setting.
All other set values, including an empty string, disable it. An unset variable
uses the setting's documented default.

The following numeric settings use inclusive bounds. Values below the minimum
or above the maximum are clamped to the nearest bound with a warning. This also
applies to numeric values too large to fit the underlying integer type.
Non-numeric values are rejected with an error; unset variables retain their defaults.

| Environment variable | Default | Minimum | Maximum | Unit |
|---|---:|---:|---:|---|
| `CACHESERVER_IB_RECEIVE_BUFFERS` | 256 | 1 | 1024 | Buffers |
| `CACHESERVER_IB_RECEIVE_THREADS` | 128 | 1 | 2048 | Threads |
| `CACHESERVER_SNAPSHOT_INTERVAL` | 300 | 1 | 7200 | Seconds |
| `CACHESERVER_THREADPOOL_SIZE` | 256 | 1 | 2048 | Threads |
| `CACHESERVER_SOCKET_SNDBUF_SIZE` | 0 | 0 | 1073741824 | Bytes (maximum 1 GiB) |
| `CACHESERVER_SOCKET_RCVBUF_SIZE` | 0 | 0 | 1073741824 | Bytes (maximum 1 GiB) |
| `TEST_CACHESERVER_ADDR_PORT` | 20000 | 1 | 65535 | Test-client TCP port |

#### CACHESERVER_USE_ZEROCOPY
Default is false. Enable splice-based zero-copy for Ethernet/socket uploads only.
This setting does not affect InfiniBand uploads.

#### CACHESERVER_USE_ZEROCOPY_IB
Default is true. Write InfiniBand uploads directly from the RDMA receive buffer
instead of copying through an intermediate buffer. Independent of `CACHESERVER_USE_ZEROCOPY`.

#### CACHESERVER_FSYNC_ENABLED
Default is false. Call `fsync()` before completing uploads, for both buffered and
zero-copy paths on Ethernet and InfiniBand. When disabled, uploaded data may be
lost or incomplete after a crash or power failure. Snapshot files are still
always synchronized, regardless of this setting.

#### CACHESERVER_IB_RECEIVE_BUFFERS
Default is 256; supported range is 1-1024. Number of receive buffers registered for incoming InfiniBand
requests, which limits concurrent requests. Each buffer requires 32 MiB + 10 KiB
of registered memory. The shared receive queue and per-buffer tracking use this
count; the completion queue grows to at least this count, with a minimum of 1024.
The count and registered memory size must fit the IB device's limits.

#### CACHESERVER_IB_RECEIVE_THREADS
Default is 128; supported range is 1-2048. Number of InfiniBand request-handler threads, configured
independently of the receive buffer count.

#### CACHESERVER_SNAPSHOT_ENABLED
Values: true or false (default is true)
Enable periodic snapshot of cache server state and loading of any previous snapshot on startup

#### CACHESERVER_WAIT_FOR_SNAPSHOT_CONSISTENCY_SCAN
Default is true. Wait for the snapshot consistency scan to finish during startup.

#### CACHESERVER_SNAPSHOT_INTERVAL
The interval in seconds between snapshots (default is 300 seconds; supported range is 1-7200 seconds).

#### CACHESERVER_HOSTNAME
The hostname to be used for the cache server (default is "")
Note: If not specified, snapshot will not be enabled

#### CACHESERVER_METRICS_PORT
The port to be used for exposing the cache server metrics (default is "")

### CACHESERVER_EVICTION_ENABLED
Values: true or false (default is false)
Enable Cache Eviction for items exceeding cache size and cache age thresholds

### CACHESERVER_EVICTION_MAINTAIN_FREE_SPACE
Values: true or false (default is false)
Enable maintain free space eviction algorithm that calculates eviction thresholds based on cache size and access patterns to maintain a target percentage of free space.
When disabled, uses the fixed threshold defined by CACHESERVER_EVICTION_LAST_ACCESS_THRESHOLD for eviction.

### CACHESERVER_EVICTION_LAST_ACCESS_THRESHOLD
Values: Last Access threshold for cache eviction, example "30d12h41m29s"
Default is set to 30d

### CACHESERVER_EVICTION_SIZE_THRESHOLD
Values: Size threshold for cache eviction in percentage (0-100)
Default is set to 90

### CACHESERVER_EVICTION_SIZE_TARGET
Values: Target cache utilization percentage after eviction (0-100)
When eviction triggers, cache will be reduced to around this percentage.
Default is 80 when deployed via Helm/Kubernetes. If the environment variable is missing or
invalid, the cache server falls back to 90% of the Eviction Size Threshold.

### CACHESERVER_EVICTION_PASS_FREQUENCY
Values: Frequency of running eviction pass, example "1h30m"
Default is 15 minutes (900s)

### CACHESERVER_EVICTION_BUCKET_INTERVAL
Values: Access-time bucket width used by the maintain-free-space (dynamic) eviction algorithm,
in the same time-interval format as the other eviction settings, example "1h", "5m", "60s".
Only used when CACHESERVER_EVICTION_MAINTAIN_FREE_SPACE is true. It controls how finely cached
entries are grouped by last-access time when computing the eviction cutoff: a smaller interval
evicts more precisely (less over-eviction), a larger interval is coarser and can over-evict.
Default is 15 minutes (900s). Invalid or too-large (> 1 week) values fall back to the default.

### CACHESERVER_THREADPOOL_SIZE
Number of Ethernet/socket worker threads. Default is 256; supported range is 1-2048.

### CACHESERVER_SOCKET_SNDBUF_SIZE
Values: Send buffer size in bytes (e.g., 4194304 for 4 MiB)
Configures SO_SNDBUF on accepted client sockets. Default is 0 (uses kernel defaults).
Set to a specific size in bytes to override kernel defaults. Supported range is
0-1073741824 bytes (1 GiB).

### CACHESERVER_SOCKET_RCVBUF_SIZE
Values: Receive buffer size in bytes (e.g., 4194304 for 4 MiB)
Configures SO_RCVBUF on accepted client sockets. Default is 0 (uses kernel defaults).
Set to a specific size in bytes to override kernel defaults. Supported range is
0-1073741824 bytes (1 GiB).

### CACHESERVER_TCP_BACKLOG_SIZE
Values: TCP listen backlog size (positive integer, default is 4096, max is 1048576)
Configures the backlog argument passed to listen() for the server socket, i.e. the
maximum number of fully established connections waiting to be accepted (the accept queue).
Invalid values (non-numeric, <= 0, or greater than 1048576) fall back to the default of 4096.
Note: the effective accept queue is also bounded by the kernel setting net.core.somaxconn.
