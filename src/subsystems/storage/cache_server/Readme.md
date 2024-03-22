## Cache server

Usage: ./CacheServer <port> <cache folder root> <cache size in bytes - default 1TB> \
<Num attributes limit - default 10M> <cache expiry ttl window size in seconds - default 60 seconds>

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

### CACHESERVER_EVICTION_LAST_ACCESS_THRESHOLD
Values: Last Access threshold for cache eviction, example "30d12h41m29s"
Default is set to 30d

### CACHESERVER_EVICTION_SIZE_THRESHOLD
Values: Size threshold for cache eviction in percentage
Default is set to 60

### CACHESERVER_EVICTION_PASS_FREQUENCY
Values: Frequency of running eviction pass, example "1h30m"
Default is set to 1 hour