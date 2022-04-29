# Cache e2e test client

## Test all APIs

### Start Cache server
./bin/Release/native/CacheServer 20000 <folder path on ssd> 100000000000 10000000 1

### Compile and run test client
g++ -std=c++17 -o2 -I . -I ../proto -I ../operations -I ../utils -I /include/ -L/usr/lib/x86_64-linux-gnu -pthread \
-lstdc++fs -o testclient TestClient.cpp  -lprotobuf -lxxhash

./testclient 

## Test cache size limits

### Start Cache server with limits set
./CacheServer 20000 <folder path on ssd> 104857600 1024 1

### Run test client
./testclient testlimits

## Cleanup
Delete CacheServer folder path after the tests
