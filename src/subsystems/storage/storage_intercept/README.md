## Storage Interception
Library to intercept remote storage filesystem calls and provide better throughput.

### Build
From the repository root:
```bash
make build
```

This builds all storage components including the StorageIntercept library. The build uses a Docker container with all dependencies pre-installed.

### Run Unit test
In a bash shell from the repository root:
```bash
make test TEST=StorageIntercept
```
If tests are passing, you should see something like this in the output. Ignore the test not found errors for cuda unit tests.
```
The following tests passed:
        StorageInterceptUnitTests

100% tests passed, 0 tests failed out of 1
```

### Run Unit test against adls
1. Set the STORAGE_INTERCEPT_USE_EXISTING_CONFIG environment variable to true
2. Add the following file at `/tmp/storageIntercept.config`
```
type adls
adlsHostname <adls-hostname>
adlsClientId <adls-app-clientId>
adlsClientSecret <adls-app-clientSecret>
adlsAuthTokenUrl <adls-app-oauth2-token-url>
```

### Run Unit test against blob
1. Set the STORAGE_INTERCEPT_USE_EXISTING_CONFIG to true
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

If the blob requests need to be authenticated using Managed Identity, use either:

Option A: Fetch access token from outside the lib and pass a static access token using
```
azBlobAccessToken <token>
```

Option B: Enable the lib to fetch token internally via MSI by configuring these settings
```
azBlobMSIEndpoint <msi-endpoint>
azBlobMSISecret <msi-secret>
azBlobIdentityClientId <identity-client-id>
```

Option C: Enable the lib to fetch token internally via [AKS Workload Identity](https://learn.microsoft.com/en-us/azure/aks/workload-identity-deploy-cluster?tabs=new-cluster)
```
azBlobIdentityClientId <managed-identity-client-id>
azBlobFederatedTokenFile <path-to-projected-sa-token-file>
azBlobTenantId <azure-ad-tenant-id>
azBlobAuthorityHost <authority-host-url> # optional, default: https://login.microsoftonline.com
```
When AKS Workload Identity is enabled, the mutating webhook automatically injects the environment variables `AZURE_FEDERATED_TOKEN_FILE`, `AZURE_TENANT_ID`, `AZURE_CLIENT_ID`, and `AZURE_AUTHORITY_HOST` into the pod. The lib reads the projected service account token from `azBlobFederatedTokenFile` and exchanges it with Azure AD for an access token using the [OAuth 2.0 client credentials flow](https://learn.microsoft.com/en-us/entra/identity-platform/v2-oauth2-client-creds-grant-flow#successful-response-1) with a JWT bearer assertion. The token file is automatically refreshed by the kubelet before expiry.

Option D: Use the azure identity SDK to fetch token
```
azBlobUseAzureIdentitySDK true
```

To enable dynamic account interception, use
```
azBlobDynamicAccount true
```
This would cause account and container name to be picked from the intercepted path. The account hostname would be the first path segment and container name would be the second path segment.
Note: Any account name, container name and blob endpoint config settings specified would be ignored.
Note: Only SAS or access token auth is supported for dynamic account mount.

To enable dynamic container interception, use
```
azBlobDynamicContainer true
```
This would cause container name to be picked from the intercepted path. The container name would be the first path segment.
Note: The container name config setting if specified would be ignored.

### Configure client metrics
Cache client metrics are initialized automatically when the cache client. Configure the metric backends with environment variables before starting the process:

| Environment variable | Default | Description |
|---|---|---|
| `STORAGE_METRIC_LOGGING_ENABLED` | `true` | Periodically write aggregated metrics to the application log. |
| `STORAGE_METRIC_LOGGING_INTERVAL_MINS` | `15` | Logging aggregation interval in minutes. |
| `STORAGE_METRIC_OTEL_ENABLED` | `false` | Export metrics using OpenTelemetry OTLP over HTTP. |
| `STORAGE_METRIC_OTEL_ENDPOINT` | `http://localhost:4318/v1/metrics` | OTLP HTTP metrics endpoint. |
| `STORAGE_METRIC_OTEL_SERVICENAME` | `dacs` | OpenTelemetry service name. |
| `STORAGE_METRIC_OTEL_EXPORT_INTERVAL_MINS` | `5` | OpenTelemetry export interval in minutes. |

For example, enable periodic logging and OTLP export with:

```bash
export STORAGE_METRIC_LOGGING_ENABLED=true
export STORAGE_METRIC_LOGGING_INTERVAL_MINS=10
export STORAGE_METRIC_OTEL_ENABLED=true
export STORAGE_METRIC_OTEL_ENDPOINT=http://otel-collector:4318/v1/metrics
export STORAGE_METRIC_OTEL_SERVICENAME=dacs-client
export STORAGE_METRIC_OTEL_EXPORT_INTERVAL_MINS=5
```

Set both `STORAGE_METRIC_LOGGING_ENABLED=false` and `STORAGE_METRIC_OTEL_ENABLED=false` to disable client metrics. Invalid or non-positive interval values use their defaults.

Some of the metrics emitted by cache client lib are:
- upload and download operation counts
- cache hit and miss counters
- DNS error counts by hostname
- successful upload/download latency histograms grouped by I/O size.

Every metric includes the process ID; when the standard distributed-training `RANK` environment variable contains a non-negative integer, it is also included as a label.

### Other config settings
Other config settings you can experiment with in `/tmp/storageIntercept.config`.

The default config file path (/tmp/storageIntercept.config) can be overridden by setting environment variable - STORAGE_INTERCEPT_CONFIG_FILEPATH before process starts.

The config key names are case insensitive.
```
enableReadIOLogs [true or false - Whether to log (INFO) every completed read request from the application]
logRefreshIntervalInSeconds [eg. 10 - Storage streamling client's log refreshes after every 10 seconds]
chunkOperationParallelismEnabled [true or false]
chunkOperationMaxConcurrency [eg. 40, should ideally be same as maxConnsPerBlobClient]
streamingChunkSize [Size of chunks big IOs are broken down into. default - 4MB]
subChunkReadThresholdPercent [Threshold percentage (of streamingChunkSize) below which partial chunk reads are allowed from cache. default - 80%]
enableLargePrefetch [true or false]
prefetchWindowSizeInMB [eg. 100]
cacheEnable [true or false - Enable data cache. default -false]
azIgnoreAccountNameInCacheKey [true or false - Ignore account name in cache key. default - false]
cacheServerPort [remote cache server port. default - 20000]
enableWrite [true or false - Enable writes. default - false ]
enableWriteRemote [true or false - Enable writes to remote store. default - true ]
enableWriteRemoteAsync [true or false - Write to cache first and then async copy to remote store. default - true ]
remoteAsyncWriteFileConcurrency [Max number of files to async copy from cache to remote store. default - 16 ]
enableVersionedCacheKeys [true or false - Include file version in cache keys and generate per-file groupIds. default - true]
enableConsistentMode [true or false - Enable consistent mode with overwrite support. Disables attributes cache (always fetches properties from Azure). 
Default is false (Default Mode: efficient mode: no overwrite, uses attribute cache if enabled). 
Requires enableVersionedCacheKeys=true. If writes are enabled(enableWrite=true), requires enableWriteRemote=true and enableWriteRemoteAsync=false.]
enableGarbageCollectionInConsistentMode [true or false - When consistent mode is enabled, controls whether stale cache chunks from overwritten files are actively deleted. When false, stale chunks are deleted by cache eviction (LRU) to clean up. default - false]
cacheUseLocalFS [true or false - Enable local ssd cache. default - false (use remote cache server)]
cacheLocalFSCachePath [local path (use ssd or tmpfs) where the cache chunks have to be stored]
cacheLocalFSCacheStagingPath [local path (use ssd or tmpfs) used for staging data currently being uploaded]
cacheLocalFSMemoryWarnThresholdGB [when local fs cache is enabled we will trigger warning messages when local writes are slow and memory back pressure exceeds this threshold]
cacheMaxUploadBufferSizePerProcessGB [limit upload buffer usage to this value. default - 0, unlimited]
cacheKeyExpirySeconds [Expiry time to be set for all the cache keys uploaded by this client. default - no expiry]
cacheKeyGroupId [GroudId to be set for all the cache keys uploaded by this client. Expiry time is ignored if this field is set. default - empty]
cacheServerList [If cache is enabled remote cache servers to use for this client, eg. node-0#node-1#node-3]
cacheEncryptionEnabled [true or false - Enable end to end encryption for data stored in cache]
cacheEncryptionKey [Key to use if encryption is enabled]
cacheIBEnabled [true or false - Use infiniband for cache client-server communication]
cacheCalculateChecksumEnabled [true or false - Enable checksum calculation for chunks before uploading them to cache server, default - true]
cacheValidateChecksumEnabled [true or false - Enable checksum validation for chunks downloaded from cache server, default - true]
subChunkDisabledForChecksumValidation [true or false - Disable subchunk reads when checksum validation is enabled so that all reads use checksum validation, default - false]
cacheChecksumAlgorithm ["xxhash32", "crc32" - Algorithm to use for checksum calculation. default - xxhash32]
cacheRetryMaxAttempts [Total number of attempts for retryable cache operations. Set to 1 to disable retries (single attempt). default - 1]
cacheRetryBaseDelayMillis [Base delay in milliseconds for exponential backoff between retries. Required when cacheRetryMaxAttempts > 1. default - 100]
cacheRetryMaxDelayMillis [Maximum delay in milliseconds between retries. Required when cacheRetryMaxAttempts > 1. default - 1000]
prefetchCacheTotalSizeInMB [Max in-memory data cache size]
prefetchCachePerFileSizeInMB [Max in-memory data cache size per file]
cacheEnableSyncUpload [After downloading chunk from remote blob on a cache miss upload to cache server synchronously in critical path. Note: This will increase client IO latency. default - false]
attributesCachePutAllOnReaddir [Upload the readdir entries to remote attributes cache. default - false]
enableZeroCopy [true or false - Try to download without any memcpy in case the user read spans over entire chunk(s). default - true]
tokenRefreshBufferInSeconds [The buffer time before token expiry to start new access token fetch in background]
tokenExpiryBufferInSeconds [ The buffer time before token expiry to consider the token as alredy expired]
tokenRequestEnableSSLVerification [true or false - Enable SSL certificate verification for token requests. default - true]
cacheServerDiscoveryEnabled [true or false - Whether to enable discovery endpoint based cache server set auto refresh or not]
cacheServerDiscoveryIntervalInSeconds [Time in seonds - Frequency of refreshing cacheserver set via discovery endpoint ]
cacheServerDiscoveryEndpoint [Endpoint as string - Cacheserver discovery endpoint]
cacheEnableConsistentHashing [true or false - Enable Consistent Hashing for Server Selection]
cacheSocketSndbufSize [SO_SNDBUF size in bytes for cache client sockets. default - 0 (uses kernel defaults). Set to a specific size in bytes to override kernel defaults]
cacheSocketRcvbufSize [SO_RCVBUF size in bytes for cache client sockets. default - 0 (uses kernel defaults). Set to a specific size in bytes to override kernel defaults]
blobRetryMaxAttempts [Total number of attempts for retryable blob operations. Set to 1 to disable retries (single attempt). default - 10]
blobRetryBaseDelaySeconds [Base delay in seconds for exponential backoff between blob retries. default - 1]
blobRetryMaxDelaySeconds [Maximum delay in seconds between blob retries. default - 128]
remoteBlobUseSdkClient [true or false - Use the azure-sdk-for-cpp remote blob client instead of the legacy cpplite client. default - false]
remoteBlobChecksumEnabled [true or false - With the SDK client, request Azure Storage CRC64 validation on blob downloads (data-integrity check). default - true]
remoteBlobUploadChecksumEnabled [true or false - With the SDK client, calculate and send CRC64 for Azure Storage validation on staged block uploads. default - true]
remoteBlobChecksumRetryMaxAttempts [Max download attempts on a CRC64 mismatch before failing; transient in-transit corruption self-heals on re-download. Only applies when remoteBlobChecksumEnabled. default - 3]
```
### Config for running interception on multiple mounts
For running interception with multiple mounts, use this sample `/tmp/storageIntercept.config`
```
multiMountEnabled true
mountName0 data
datastoragePath /mnt/dataMount
datatype blob
dataazBlobStorageAccountName <account1Name>
dataazBlobSASToken ${DATA_CONTAINER_SAS_TOKEN}
dataazBlobStorageEndpoint https://<account1Name>.blob.core.windows.net
dataazBlobContainerName dataContainer
datamaxConnsPerBlobClient 30
datacacheEnable true
mountName1 code
codestoragePath /mnt/codeMount
codetype blob
codeazBlobStorageAccountName <account2Name>
codeazBlobSASToken ${CODE_CONTAINER_SAS_TOKEN2}
codeazBlobStorageEndpoint https://<account2Name>.blob.core.windows.net
codeazBlobContainerName codeContainer
codemaxConnsPerBlobClient 10
codecacheEnable false
cacheServerList node-0#node-1#node-3
cacheServerPort 9065
```

### Run Unit test with cache enabled
1. Start cache_server at port 20000 in the same container as the client
2. Set the STORAGE_INTERCEPT_USE_EXISTING_CONFIG to true
3. Add/edit the following to existing configuration at '/tmp/storageIntercept.config'
```
cacheEnable true
cacheServerPort 20000
cacheNumUploadThreadsPerProcess 2
cacheMaxUploadBufferSizePerProcessGB 1
cacheEnableRemote false
cacheUseLocalFS false
cacheLocalFSCachePath /tmp/_cache
cacheNumServers 1
cacheReuseServerConnections true
cacheMaxConnectionsReuseSize 1024
```
4. Execute the tests from bash shell
```bash
make test TEST=StorageIntercept
```

### Run integration test
To run integration tests, repeat the 2nd step mentioned in the section: "`Run Unit test against adls`". \
In the `/tmp/storageIntercept.config`, make sure to add / edit this config property:
```
...
storagePath /mnt/storage
```
cd to the `integration_tests` directory.

*There are two integration tests:*

1. Using synthetic testing on two kinds of datasets: small (file sizes ~100 - 200KB) or large (file sizes ~100 - 400MB).
2. Using DL job run using distributed data parallelism with storage interception.

Run `./synthetic_test_runner.sh -h` for checking the usage, and running the synthetic test with different options. \
Run `./dl_job_test.sh -h` for checking the usage, and running an E2E DL job with different options.

### Override config key values
Any config key defined in storageIntercept.config can be overridden using environment variable SI_<config key name>.

For example if SI_cacheEnable environment variable is set for the process running storage interception then the value for the same key defined in storageIntercept.config will be ignored.