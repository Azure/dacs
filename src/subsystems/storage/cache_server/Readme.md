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