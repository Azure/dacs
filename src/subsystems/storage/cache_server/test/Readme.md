# Cache e2e test client

## Test all APIs

### Start Cache server
./bin/Release/native-storage/storage/CacheServer 20000 <folder path on ssd> 100000000000 10000000 1

### Run test client
./bin/Release/native-storage/storage/cacheServerTestClient

## Test cache size limits

### Start Cache server with limits set
./CacheServer 20000 <folder path on ssd> 104857600 1024 1

### Run test client
./bin/Release/native-storage/storage/cacheServerTestClient testlimits

## Supported environment variables:

#### TEST_CACHESERVER_ADDR_HOST
Address of the cache server to run tests against (default is ""127.0.0.1"")

#### TEST_CACHESERVER_ADDR_PORT
Port of the cache server to run tests against (default is 20000)

#### TEST_IB
Values: true or false (default is false).
Enable Infiniband tests
