## Cache server

Usage: ./CacheServer <port> <cache folder root> <cache size in bytes - default 1TB> \
<unused - reserved> <cache expiry ttl window size in seconds - default 60 seconds>

### Supported environment variables:

#### CACHESERVER_AUTH_ENABLED
Values: true or false
Enable authentication for all cache server APIs

#### CACHESERVER_AUTH_ACCOUNTNAME
The account name to be used for authentication

#### CACHESERVER_AUTH_ACCOUNTKEY
The shared key required to validate client requests

#### CACHESERVER_SNAPSHOT_ENABLED
Values: true or false (default is true)
Enable periodic snapshot of cache server state and loading of any previous snapshot on startup

#### CACHESERVER_SNAPSHOT_INTERVAL
The interval in seconds between snapshots (default is 300 seconds)

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

### CACHESERVER_SOCKET_SNDBUF_SIZE
Values: Send buffer size in bytes (e.g., 4194304 for 4 MiB)
Configures SO_SNDBUF on accepted client sockets. Default is 0 (uses kernel defaults).
Set to a specific size in bytes to override kernel defaults.

### CACHESERVER_SOCKET_RCVBUF_SIZE
Values: Receive buffer size in bytes (e.g., 4194304 for 4 MiB)
Configures SO_RCVBUF on accepted client sockets. Default is 0 (uses kernel defaults).
Set to a specific size in bytes to override kernel defaults.

### CACHESERVER_TCP_BACKLOG_SIZE
Values: TCP listen backlog size (positive integer, default is 4096, max is 1048576)
Configures the backlog argument passed to listen() for the server socket, i.e. the
maximum number of fully established connections waiting to be accepted (the accept queue).
Invalid values (non-numeric, <= 0, or greater than 1048576) fall back to the default of 4096.
Note: the effective accept queue is also bounded by the kernel setting net.core.somaxconn.
