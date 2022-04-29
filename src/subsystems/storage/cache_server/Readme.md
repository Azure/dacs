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
