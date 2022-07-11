## Storage Interception
Library to intercept remote storage filesystem calls and provide better throughput.

### Build
In a bash shell
```
cd <path to repository root>
./bldenv.sh
```
Above command would automatically boot up a docker container with the latest image, and open a shell attached to it. Some packages being used with recent changes are required to be installed (as they're not part of the latest image yet). \
Run (only needed to run E2E pytorch job):
```
pip3 install setuptools
apt-get install libjpeg-dev
pip3 install torch torchvision torchsummary
```
To do a full build:
```
.pipelines/restore.sh
.pipelines/build.sh
```
To do a partial build:
```
dotnet msbuild -p:Configuration=Debug -p:SkipCuda11Build=true -M /mnt/DACS/DACS/src/native/native_build.linuxcmakeproj
```

### Run Unit test
In a bash shell
```
.pipelines/nativeunittest.sh Debug StorageIntercept
```
If tests are passing, you should see something like this in the output. Ignore the test not found errors for cuda unit tests.
```
The following tests passed:
        StorageInterceptUnitTests

100% tests passed, 0 tests failed out of 1
```

### Run Unit test against adls
1. Set the STORAGE_INTERCEPT_USE_EXISITNG_CONFIG environment variable to true
2. Add the following file at `/tmp/storageIntercept.config`
```
type adls
adlsHostname <adls-hostname>
adlsClientId <adls-app-clientId>
adlsClientSecret <adls-app-clientSecret>
adlsAuthTokenUrl <adls-app-oauth2-token-url>
```

### Run Unit test against blob
1. Set the STORAGE_INTERCEPT_USE_EXISITNG_CONFIG to true
2. Add the following file at `/tmp/storageIntercept.config`
```
type blob
azBlobStorageAccountName <storage-account-name>
azBlobStorageEndpoint <storage-account-blob-endpoint>
azBlobContainerName <storage-account-container>
maxConnsPerBlobClient [eg. 10 or 100]
```

If the blob requests need to be authenticated using account key, use
```
azBlobStorageAccountKey <storage-account-key>
```

If the blob requests need to be authenticated using SASToken, use
```
azBlobSASToken <any-type-of-sas-token>
```

If the blob requests need to be authenticated using an accessToken obtained from Managed Identity, use
```
azBlobAccessToken <token>
```

### Other config settings
Other config settings you can experiment with in `/tmp/storageIntercept.config`
```
chunkDownloadParallelismEnabled [true or false]
chunkDownloadMaxConcurrency [eg. 40, should ideally be same as maxConnsPerBlobClient]
enableLargePrefetch [true or false]
prefetchWindowSizeInMB [eg. 100]
cacheServerList [If cache is enabled remote cache servers to use for this client, eg. node-0#node-1#node-3]
cacheEncryptionEnabled [true or false - Enable end to end encryption for data stored in cache]
cacheEncryptionKey [Key to use if encryption is enabled]
cacheAuthEnabled [true or false - Required when cache server APIs have authentication enabled]
cacheAuthAccountName [Account name to be used for authenticatio]
cacheAuthAccountKey [Shared key used for authentication]
prefetchCacheTotalSizeInMB [Max in-memory data cache size]
prefetchCachePerFileSizeInMB [Max in-memory data cache size per file]
```

### Run Unit test with cache enabled
1. Start cache_server at port 20000 in the same container as the client
2. Set the STORAGE_INTERCEPT_USE_EXISITNG_CONFIG to true
3. Add/edit the following to existing configuration at '/tmp/storageIntercept.config'
```
cacheEnable true
cacheServerPort 20000
cacheNumUploadThreadsPerProcess 2
cacheMaxUploadBufferSizePerProcess 1073741824
cacheEnableRemote false
cacheUseLocalFS false
cacheLocalFSCachePath /tmp/_cache
cacheNumServers 1
cacheReuseServerConnections true
```
4. Execute the tests from bash shell
```
.pipelines/nativeunittest.sh Debug StorageIntercept
```

### Run Unit test with attribute cache enabled
1. Start cache_server at port 20000 in the same container as the client
2. Set the STORAGE_INTERCEPT_USE_EXISITNG_CONFIG to true
3. Add/edit the following to existing configuration at '/tmp/storageIntercept.config'
4. (Optional) Set the STORAGE_INTERCEPT_TESTCASES_DIR env to the directory that contains test cases
```
attributesCacheEnable true
attributesCacheUseLocalServer true
```
4. Execute the tests from bash shell
```
.pipelines/nativeunittest.sh Debug StorageIntercept
```

### Run integration test
To run integration tests, repeat the 2nd step mentioned in the section: "`Run Unit test against adls`". \
In the `/tmp/storageIntercept.config`, make sure to add / edit this config property:
```
...
storagePath /mnt/quiverfs
```
cd to the `integration_tests` directory.

*There are two integration tests:*

1. Using synthetic testing on two kinds of datasets: small (file sizes ~100 - 200KB) or large (file sizes ~100 - 400MB).
2. Using DL job run using distributed data parallelism with storage interception.

Run `./synthetic_test_runner.sh -h` for checking the usage, and running the synthetic test with different options. \
Run `./dl_job_test.sh -h` for checking the usage, and running an E2E DL job with different options.
