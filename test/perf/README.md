# DACS Client E2E: Running with Ginkgo

This guide explains how to run the dacs cache client end-to-end tests using Ginkgo.

## Prerequisites
- Provision Kubernetes cluster and update current-context to point to created cluster
- Create storage account in Azure if not already exist
- Assign `Storage Blob Data Contributor` role to kubelet identity on created storage account (Run from root directory of project)
```sh
 az deployment group create --template-file deploy/template/storage.bicep --resource-group <resource_group> --parameters storageAccountName=<storage_account_name> aksName=<aks_cluster_name> aksResourceGroup=<aks_cluster_resource_group>
```
- Install ginkgo by running following command from the root of project repository
```sh
make ginkgo
```

## Run
- Navigate to the project root:
```sh
cd <Root Directory of DACS Project>
```
- Set `STORAGE_ACCOUNT` env to storage account name
- Run the dacs clients suite:
  ```sh
  make test-e2e TEST_FILTER="perf && dacsclients"
  ```
  (Or) other alternate way is
  ```sh
  ginkgo -v -r --timeout="60m" --label-filter="perf && (dacsclients)" --focus="" --no-color="false" "./test/perf" -- -- -blobCount=50 -clientImage=dacs.azurecr.io/cache-test-client:local -storageAccount <storage_account_name>
  ```

Notes:
- `-clientImage`: specify the cache client image to test.
- `-storageAccount`: the Azure Storage account name (or) set the `STORAGE_ACCOUNT` env to storage account name.
- Optional flags: `-blobCount`, `-blobSize`, `count` (No.of cache clients defaults to no.of kubernetes nodes) and `writeMode`(defaults to write_sync).



### Authentication
- If `STORAGE_INTCPT_SAS_TOKEN` is set, the tests use SAS authentication.
- Otherwise, kubelet identity is used. Test will create `Storage Blob Data Contributor` role on the target account.


## Sample Output

```sh
<<<<<<< HEAD
mittas@sample-devbox:~/workspace/azure/DACS$ BUILD_NUMBER=latest DOCKER_REGISTRY=dacs.azurecr.io STORAGE_ACCOUNT=samplestorageacct make test-e2e TEST_FILTER="perf && dacsclients"

GIT_COMMIT=e3f5968cd34e53cc1dd57efaf460f67e2e48fd7a GIT_BRANCH=sample/multicache-client PATH=/home/sampleuser/workspace/azure/DACS/.local/bin:$PATH /home/sampleuser/workspace/azure/DACS/.local/bin/ginkgo -v -r --timeout="60m" --label-filter="perf && (dacsclients)" --focus="" --no-color="false" "./test/perf" -- --junit-report=/home/sampleuser/workspace/azure/DACS/test.xml --summary-report=/home/sampleuser/workspace/azure/DACS/summary.md --support-bundle-dir=/home/sampleuser/workspace/azure/DACS/support-bundles
  I0124 16:32:16.416184  264576 perf_suite_test.go:80] "Starting DACS Performance test suite"
=======
 BUILD_NUMBER=latest DOCKER_REGISTRY=dacs.azurecr.io STORAGE_ACCOUNT=samplestorageacct LABEL_FILTER=dacsclients make test-perf EXTRA_TEST_ARGS="--client-image=dacs.azurecr.io/cache-test-client:latest --blob-count=20 --blob-size=536870912"
Using system kubectl
GIT_COMMIT=75661283f4ae11826f796ee4d1a13c390430d155 GIT_BRANCH=sample/perf-branch PATH=/home/sampleuser/workspace/azure/DACS/.local/bin:$PATH /home/sampleuser/workspace/azure/DACS/.local/bin/ginkgo -v -r --timeout="60m" --label-filter="perf && (dacsclients)" --focus="" --no-color="false" "./test/perf" -- --junit-report=/home/sampleuser/workspace/azure/DACS/test.xml --summary-report=/home/sampleuser/workspace/azure/DACS/summary.md --support-bundle-dir=/home/sampleuser/workspace/azure/DACS/support-bundles --client-image=dacs.azurecr.io/cache-test-client:latest --blob-count=20 --blob-size=536870912
  I0210 06:14:38.589093 1623035 perf_suite_test.go:80] "Starting DACS Performance test suite"
>>>>>>> 4c6fd8c1 (chore: move dacs client to run as MPI workers)
Running Suite: perf suite - /home/sampleuser/workspace/azure/DACS/test/perf
==========================================================================
Random Seed: 1770704074

Will run 1 of 6 specs
------------------------------
[BeforeSuite]
/home/sampleuser/workspace/azure/DACS/test/perf/perf_suite_test.go:84
  I0210 06:14:38.591808 1623035 utils.go:55] "running command" command="az --version"
  I0210 06:14:40.118540 1623035 common.go:87] "az cli version" version=<
        WARNING: You have 2 update(s) available. Consider updating your CLI installation with 'az upgrade'
        azure-cli                         2.81.0 *

        core                              2.81.0 *
        telemetry                          1.1.0

        Extensions:
        aks-preview                    19.0.0b18
        k8s-extension                      1.7.0

        Dependencies:
        msal                            1.34.0b1
        azure-mgmt-resource               23.3.0

        Python location '/opt/az/bin/python3'
        Config directory '/home/sampleuser/.azure'
        Extensions directory '/home/sampleuser/.azure/cliextensions'

        Python (Linux) 3.13.9 (main, Nov 26 2025, 00:44:06) [GCC 13.3.0]

        Legal docs and information: aka.ms/AzureCliLegal


   >
  I0210 06:14:40.119003 1623035 utils.go:55] "running command" command="kubectl version"
  I0210 06:14:40.584738 1623035 common.go:92] "kubectl version" version=<
        Client Version: v1.35.0
        Kustomize Version: v5.7.1
        Server Version: v1.33.5
        Warning: version difference between client (1.35) and server (1.33) exceeds the supported minor version skew of +/-1
   >
  I0210 06:14:40.584881 1623035 common.go:102] "Skipping installation (set INSTALL=true to install)"
  STEP: validating that cache server pods are running @ 02/10/26 06:14:40.584
  STEP: validating that the pod with label app=cacheserver-scheduler is running as expected @ 02/10/26 06:14:40.585
  I0210 06:14:40.681141 1623035 common.go:476] "Checking pod status" pod="cacheserver-scheduler-77f67d57c7-g24sj"
  STEP: validating that StatefulSet cacheserver has all replicas ready @ 02/10/26 06:14:40.681
  I0210 06:14:40.689099 1623035 common.go:442] "StatefulSet is ready" name="cacheserver" ready=3 total=3
[BeforeSuite] PASSED [2.098 seconds]
------------------------------
SS
------------------------------
Multiple DACS Clients E2E Tests Multiple DACS Clients Test Suite Should run multiple dacs cache clients performing writes [dacsclients, perf, aks]
/home/sampleuser/workspace/azure/DACS/test/perf/dacs_client_suite_test.go:132
  STEP: Installing mpi-operator in the cluster @ 02/10/26 06:14:40.69
  I0210 06:14:40.691042 1623035 utils.go:55] "running command" command="kubectl apply --server-side -f https://raw.githubusercontent.com/kubeflow/mpi-operator/master/deploy/v2beta1/mpi-operator.yaml"
  I0210 06:14:42.697607 1623035 utils.go:55] "running command" command="kubectl get pod -o jsonpath='{.items[*].metadata.name},' -n mpi-operator"
  I0210 06:14:42.978555 1623035 utils.go:55] "running command" command="kubectl get pod mpi-operator-5d8b594fbb-w5g2s -n mpi-operator -o jsonpath={.status.phase}"
  I0210 06:14:48.275264 1623035 utils.go:55] "running command" command="kubectl get pod -o jsonpath='{.items[*].metadata.name},' -n mpi-operator"
  I0210 06:14:48.580332 1623035 utils.go:55] "running command" command="kubectl get pod mpi-operator-5d8b594fbb-w5g2s -n mpi-operator -o jsonpath={.status.phase}"
  I0210 06:14:49.446432 1623035 utils.go:55] "running command" command="kubectl get ns dacs-clients"
  E0210 06:14:49.722171 1623035 utils.go:58] "command failed" err="exit status 1" command="kubectl get ns dacs-clients" output=<
        Error from server (NotFound): namespaces "dacs-clients" not found
   >
  I0210 06:14:49.722481 1623035 utils.go:55] "running command" command="kubectl create ns dacs-clients"
  I0210 06:14:50.059428 1623035 utils.go:55] "running command" command="az storage container exists --name dacs-perf-2026-02-10t06-14-50z --account-name samplestorageacct --query exists -o tsv"
  I0210 06:14:56.831436 1623035 utils.go:55] "running command" command="az storage container create --name dacs-perf-2026-02-10t06-14-50z --account-name samplestorageacct"
  I0210 06:15:01.130183 1623035 utils.go:55] "running command" command="az aks list --query [?name=='sample-cluster'].resourceGroup -o tsv"
  STEP: Checking if managed identity with clientId 00000000-0000-0000-0000-000000000000 has required role assignment on storage account samplestorageacct @ 02/10/26 06:15:15.065
  I0210 06:15:15.066186 1623035 utils.go:55] "running command" command="az storage account show --name samplestorageacct --query id -o tsv"
  I0210 06:15:19.286428 1623035 utils.go:55] "running command" command="az ad sp show --id 00000000-0000-0000-0000-000000000000 --query id -o tsv"
  I0210 06:15:20.700080 1623035 utils.go:55] "running command" command="az role assignment list --assignee 00000000-0000-0000-0000-000000000000 --scope /subscriptions/00000000-0000-0000-0000-000000000000/resourceGroups/sample-rg/providers/Microsoft.Storage/storageAccounts/samplestorageacct --role Storage Blob Data Contributor --query [].id -o tsv"
  I0210 06:15:24.521288 1623035 utils.go:55] "running command" command="kubectl apply -f -"
  STEP: Waiting for dacs-client-mpi-4 job to complete @ 02/10/26 06:15:25.133
  I0210 06:15:25.133976 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0210 06:15:25.430754 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Succeeded\")].status}"
  I0210 06:15:30.918847 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0210 06:15:31.403157 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Succeeded\")].status}"
  I0210 06:15:36.892946 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0210 06:15:37.208789 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Succeeded\")].status}"
  I0210 06:15:42.490086 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0210 06:15:43.843136 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Succeeded\")].status}"
  I0210 06:15:49.167817 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0210 06:15:49.463744 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Succeeded\")].status}"
  I0210 06:15:54.771283 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0210 06:15:55.088411 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Succeeded\")].status}"
  I0210 06:16:00.373737 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0210 06:16:00.658123 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Succeeded\")].status}"
  I0210 06:16:05.951623 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0210 06:16:06.468895 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Succeeded\")].status}"
  I0210 06:16:11.752216 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0210 06:16:12.040127 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Succeeded\")].status}"
  I0210 06:16:17.353077 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0210 06:16:17.661441 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Succeeded\")].status}"
  I0210 06:16:22.966781 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0210 06:16:23.289700 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Succeeded\")].status}"
  I0210 06:16:28.583128 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0210 06:16:28.902121 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Succeeded\")].status}"
  I0210 06:16:34.184704 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0210 06:16:34.472366 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Succeeded\")].status}"
  I0210 06:16:39.767535 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0210 06:16:40.057063 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Succeeded\")].status}"
  I0210 06:16:45.338229 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0210 06:16:45.622704 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Succeeded\")].status}"
  I0210 06:16:50.918210 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0210 06:16:51.233155 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Succeeded\")].status}"
  I0210 06:16:56.523713 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0210 06:16:56.819762 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Succeeded\")].status}"
  I0210 06:17:02.111610 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0210 06:17:02.398525 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Succeeded\")].status}"
  I0210 06:17:07.680288 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0210 06:17:07.977546 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Succeeded\")].status}"
  I0210 06:17:08.257154 1623035 utils.go:55] "running command" command="kubectl get mpijob dacs-client-mpi-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Succeeded\")].status}"
  I0210 06:17:08.640950 1623035 utils.go:55] "running command" command="kubectl logs dacs-client-mpi-4-launcher-m55r7 -n dacs-clients"
  STEP: Collecting test result of Pod: 0 dacs-clients/dacs-client-mpi-4-worker-0 through launcher pod dacs-clients/dacs-client-mpi-4-launcher-m55r7 logs @ 02/10/26 06:17:09.676
  I0210 06:17:09.855498 1623035 utils.go:55] "running command" command="kubectl get node aks-manipool1-53964174-vmss000001 -o jsonpath={.metadata.labels.node\\.kubernetes\\.io/instance-type}"
  STEP: Collecting test result of Pod: 1 dacs-clients/dacs-client-mpi-4-worker-1 through launcher pod dacs-clients/dacs-client-mpi-4-launcher-m55r7 logs @ 02/10/26 06:17:10.129
  I0210 06:17:10.298727 1623035 utils.go:55] "running command" command="kubectl get node aks-manipool1-53964174-vmss000000 -o jsonpath={.metadata.labels.node\\.kubernetes\\.io/instance-type}"
  STEP: Collecting test result of Pod: 2 dacs-clients/dacs-client-mpi-4-worker-2 through launcher pod dacs-clients/dacs-client-mpi-4-launcher-m55r7 logs @ 02/10/26 06:17:10.581
  I0210 06:17:10.758357 1623035 utils.go:55] "running command" command="kubectl get node aks-manipool1-53964174-vmss000008 -o jsonpath={.metadata.labels.node\\.kubernetes\\.io/instance-type}"
  STEP: Collecting test result of Pod: 3 dacs-clients/dacs-client-mpi-4-worker-3 through launcher pod dacs-clients/dacs-client-mpi-4-launcher-m55r7 logs @ 02/10/26 06:17:11.058
  I0210 06:17:11.230622 1623035 utils.go:55] "running command" command="kubectl get node aks-manipool1-53964174-vmss000002 -o jsonpath={.metadata.labels.node\\.kubernetes\\.io/instance-type}"
  STEP: Aggregated Cache Client Performance Summary: TotalBlobsWritten: 80, TotalBytesWritten: 42949672960, TotalWriteTimeSeconds: 52.94, WriteThroughputGbps: 24.19, TotalBytesRead: 42949672960, TotalReadTimeSeconds: 44.29, ReadThroughputGbps: 28.92 @ 02/10/26 06:17:11.521
  STEP: Logging performance metrics @ 02/10/26 06:17:11.522
  I0210 06:17:11.522146 1623035 utils.go:55] "running command" command="helm list -n cache-server -o json"
  I0210 06:17:12.399799 1623035 kusto.go:165] "Perf metric" metric=<
        {
          "RunID": "",
          "SuiteID": "00000000-0000-0000-0000-000000000000",
          "Timestamp": "2026-02-10T06:17:12Z",
          "TestName": "Multiple DACS Clients E2E Tests Multiple DACS Clients Test Suite Should run multiple dacs cache clients performing writes",
          "TestGitCommit": "75661283f4ae11826f796ee4d1a13c390430d155",
          "TestGitBranch": "sample/perf-branch",
          "HelmChartVersion": "cache-server-0.1.0",
          "HelmAppVersion": "1.16.0",
          "TestMetadata": {
            "StorageAccount": "samplestorageacct",
            "ContainerName": "dacs-perf-2026-02-10t06-14-50z",
            "ClusterName": "sample-cluster",
            "JobName": "dacs-client-mpi-4",
            "IsMinikube": false,
            "WriteMode": "write_sync",
            "Clients": 4,
            "PodsMetadata": [
              {
                "PodName": "dacs-client-mpi-4-worker-0",
                "NodeName": "aks-manipool1-53964174-vmss000001",
                "NodeSKU": "Standard_L8as_v3",
                "PerfSummary": {
                  "BlobSize": 0,
                  "TotalBlobsWritten": 20,
                  "TotalBytesWritten": 10737418240,
                  "TotalWriteTimeSeconds": 13.42,
                  "WriteThroughputGbps": 5.959842750559369,
                  "TotalBytesRead": 10737418240,
                  "TotalReadTimeSeconds": 11.05,
                  "ReadThroughputGbps": 7.2384613672106966
                }
              },
              {
                "PodName": "dacs-client-mpi-4-worker-1",
                "NodeName": "aks-manipool1-53964174-vmss000000",
                "NodeSKU": "Standard_L8as_v3",
                "PerfSummary": {
                  "BlobSize": 0,
                  "TotalBlobsWritten": 20,
                  "TotalBytesWritten": 10737418240,
                  "TotalWriteTimeSeconds": 13.57,
                  "WriteThroughputGbps": 5.895850718011859,
                  "TotalBytesRead": 10737418240,
                  "TotalReadTimeSeconds": 10.77,
                  "ReadThroughputGbps": 7.431389649315301
                }
              },
              {
                "PodName": "dacs-client-mpi-4-worker-2",
                "NodeName": "aks-manipool1-53964174-vmss000008",
                "NodeSKU": "Standard_L8as_v3",
                "PerfSummary": {
                  "BlobSize": 0,
                  "TotalBlobsWritten": 20,
                  "TotalBytesWritten": 10737418240,
                  "TotalWriteTimeSeconds": 12.95,
                  "WriteThroughputGbps": 6.179253804582445,
                  "TotalBytesRead": 10737418240,
                  "TotalReadTimeSeconds": 11.61,
                  "ReadThroughputGbps": 6.88868642772152
                }
              },
              {
                "PodName": "dacs-client-mpi-4-worker-3",
                "NodeName": "aks-manipool1-53964174-vmss000002",
                "NodeSKU": "Standard_L8as_v3",
                "PerfSummary": {
                  "BlobSize": 0,
                  "TotalBlobsWritten": 20,
                  "TotalBytesWritten": 10737418240,
                  "TotalWriteTimeSeconds": 13,
                  "WriteThroughputGbps": 6.152980407097097,
                  "TotalBytesRead": 10737418240,
                  "TotalReadTimeSeconds": 10.86,
                  "ReadThroughputGbps": 7.363876208149876
                }
              }
            ]
          },
          "Result": {
            "BlobSize": 0,
            "TotalBlobsWritten": 80,
            "TotalBytesWritten": 42949672960,
            "TotalWriteTimeSeconds": 52.94,
            "WriteThroughputGbps": 24.18792768025077,
            "TotalBytesRead": 42949672960,
            "TotalReadTimeSeconds": 44.29,
            "ReadThroughputGbps": 28.922413652397395
          }
        }
   >
  I0210 06:17:12.401447 1623035 kusto.go:169] "SEND_METRICS not set to 'true', skipping EventHub send"
  I0210 06:17:12.401588 1623035 utils.go:55] "running command" command="az storage container delete --name dacs-perf-2026-02-10t06-14-50z --account-name samplestorageacct"
  I0210 06:17:17.073896 1623035 utils.go:55] "running command" command="kubectl delete ns dacs-clients --ignore-not-found --wait"
  STEP: Uninstalling mpi-operator from the cluster @ 02/10/26 06:17:28.389
  I0210 06:17:28.390527 1623035 utils.go:55] "running command" command="kubectl delete -f https://raw.githubusercontent.com/kubeflow/mpi-operator/master/deploy/v2beta1/mpi-operator.yaml"
• [174.195 seconds]
------------------------------
SSS
------------------------------
[AfterSuite]
/home/sampleuser/workspace/azure/DACS/test/perf/perf_suite_test.go:88
  I0210 06:17:34.886123 1623035 perf_suite_test.go:101] "Cleaning up test jobs"
  I0210 06:17:34.886407 1623035 utils.go:55] "running command" command="kubectl delete jobs -n cache-server -l dacs.azure.com/test-pod=true --ignore-not-found=true"
  STEP: collecting support bundle @ 02/10/26 06:17:35.178
  STEP: cleaning up test jobs @ 02/10/26 06:17:42.118
  I0210 06:17:42.399109 1623035 common.go:163] "Test jobs cleaned up successfully" output=<
        No resources found
   >
  I0210 06:17:42.399190 1623035 common.go:167] "Skipping uninstall (set UNINSTALL=true to uninstall)"
[AfterSuite] PASSED [7.513 seconds]
------------------------------
[ReportAfterSuite] perf reporter
/home/sampleuser/workspace/azure/DACS/test/perf/perf_suite_test.go:93
  STEP: Collecting support bundle for the suite @ 02/10/26 06:17:42.399
  I0210 06:17:42.399584 1623035 common.go:407] "Collecting support bundle for suite" path="/home/sampleuser/workspace/azure/DACS/support-bundles/suite.tar.gz"
  STEP: generating junit report @ 02/10/26 06:17:49.395
  STEP: generating path to absolute path: /home/sampleuser/workspace/azure/DACS/test.xml @ 02/10/26 06:17:49.395
  STEP: Generating markdown report @ 02/10/26 06:17:49.398
  I0210 06:17:49.399073 1623035 utils.go:55] "running command" command="kubectl config current-context"
[ReportAfterSuite] PASSED [7.237 seconds]
------------------------------

Ran 1 of 6 Specs in 183.808 seconds
SUCCESS! -- 1 Passed | 0 Failed | 0 Pending | 5 Skipped
PASS

Ginkgo ran 1 suite in 3m15.073097053s
Test Suite Passed------------------------------
SS
------------------------------
[AfterSuite]
/home/sampleuser/workspace/azure/DACS/test/perf/perf_suite_test.go:88
  I0124 16:36:21.944382  264576 perf_suite_test.go:101] "Cleaning up test jobs"
  I0124 16:36:21.944689  264576 utils.go:55] "running command" command="kubectl delete jobs -n cache-server -l dacs.azure.com/test-pod=true --ignore-not-found=true"
  STEP: collecting support bundle @ 01/24/26 16:36:22.229
  I0124 16:36:26.995121  264576 common.go:148] "Skipping uninstall (set UNINSTALL=true to uninstall)"
[AfterSuite] PASSED [5.051 seconds]
------------------------------
[ReportAfterSuite] perf reporter
/home/sampleuser/workspace/azure/DACS/test/perf/perf_suite_test.go:93
  STEP: Collecting support bundle for the suite @ 01/24/26 16:36:26.995
  I0124 16:36:26.995737  264576 common.go:292] "Collecting support bundle for suite" path="/home/sampleuser/workspace/azure/DACS/support-bundles/suite.tar.gz"
  STEP: generating junit report @ 01/24/26 16:36:31.472
  STEP: generating path to absolute path: /home/sampleuser/workspace/azure/DACS/test.xml @ 01/24/26 16:36:31.472
  STEP: Generating markdown report @ 01/24/26 16:36:31.475
  I0124 16:36:31.475858  264576 utils.go:55] "running command" command="kubectl config current-context"
[ReportAfterSuite] PASSED [4.714 seconds]
------------------------------

Ran 1 of 6 Specs in 250.578 seconds
SUCCESS! -- 1 Passed | 0 Failed | 0 Pending | 5 Skipped
PASS

Ginkgo ran 1 suite in 4m18.044178968s
Test Suite Passed
```