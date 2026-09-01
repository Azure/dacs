# Cache e2e test client

## Test all APIs

### Start Cache server

./bin/Release/native-storage/storage/CacheServer --port 20000 --cacheRoot <folder path on ssd> --sizeLimitBytes 100000000000 --ttlWindowSeconds 1

### Run test client
./bin/Release/native-storage/storage/cacheServerTestClient

## Test cache size limits

### Start Cache server with limits set

./CacheServer --port 20000 --cacheRoot <folder path on ssd> --sizeLimitBytes 104857600 --ttlWindowSeconds 1

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

#### TEST_IB_ONLY
Values: true or false (default is false).
Run only Infiniband tests, skipping all other tests (implies TEST_IB=true)
