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
  LABEL_FILTER=dacsclients make test-perf
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
mittas@sample-devbox:~/workspace/azure/DACS$ BUILD_NUMBER=latest DOCKER_REGISTRY=dacs.azurecr.io STORAGE_ACCOUNT=samplestorageacct LABEL_FILTER=dacsclients make test-perf

GIT_COMMIT=e3f5968cd34e53cc1dd57efaf460f67e2e48fd7a GIT_BRANCH=sample/multicache-client PATH=/home/sampleuser/workspace/azure/DACS/.local/bin:$PATH /home/sampleuser/workspace/azure/DACS/.local/bin/ginkgo -v -r --timeout="60m" --label-filter="perf && (dacsclients)" --focus="" --no-color="false" "./test/perf" -- --junit-report=/home/sampleuser/workspace/azure/DACS/test.xml --summary-report=/home/sampleuser/workspace/azure/DACS/summary.md --support-bundle-dir=/home/sampleuser/workspace/azure/DACS/support-bundles
  I0124 16:32:16.416184  264576 perf_suite_test.go:80] "Starting DACS Performance test suite"
Running Suite: perf suite - /home/sampleuser/workspace/azure/DACS/test/perf
==========================================================================
Random Seed: 1769272333

Will run 1 of 6 specs
------------------------------
[BeforeSuite]
/home/sampleuser/workspace/azure/DACS/test/perf/perf_suite_test.go:84
  I0124 16:32:16.419214  264576 utils.go:55] "running command" command="az --version"
  I0124 16:32:17.460568  264576 common.go:84] "az cli version" version=<
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
  I0124 16:32:17.461138  264576 utils.go:55] "running command" command="kubectl version"
  I0124 16:32:17.767158  264576 common.go:89] "kubectl version" version=<
        Client Version: v1.35.0
        Kustomize Version: v5.7.1
        Server Version: v1.33.5
        Warning: version difference between client (1.35) and server (1.33) exceeds the supported minor version skew of +/-1
   >
  I0124 16:32:17.767273  264576 common.go:92] "Skipping installation (set INSTALL=true to install)"
  STEP: validating that cache server pods are running @ 01/24/26 16:32:17.767
  STEP: validating that the pod with label app=cacheserver-scheduler is running as expected @ 01/24/26 16:32:17.767
  I0124 16:32:17.835310  264576 common.go:358] "Checking pod status" pod="cacheserver-scheduler-fbfbf9559-qwnp4"
  STEP: validating that StatefulSet cacheserver has all replicas ready @ 01/24/26 16:32:17.835
  I0124 16:32:17.845280  264576 common.go:324] "StatefulSet is ready" name="cacheserver" ready=3 total=3
[BeforeSuite] PASSED [1.427 seconds]
------------------------------
SSS
------------------------------
Multiple DACS Clients E2E Tests Multiple DACS Clients Test Suite Should run multiple dacs cache clients performing writes [dacsclients, perf, aks]
/home/sampleuser/workspace/azure/DACS/test/perf/dacs_client_suite_test.go:94
  I0124 16:32:17.846179  264576 utils.go:55] "running command" command="kubectl get ns dacs-clients"
  E0124 16:32:18.128057  264576 utils.go:58] "command failed" err="exit status 1" command="kubectl get ns dacs-clients" output=<
        Error from server (NotFound): namespaces "dacs-clients" not found
   >
  I0124 16:32:18.128316  264576 utils.go:55] "running command" command="kubectl create ns dacs-clients"
  I0124 16:32:18.699953  264576 utils.go:55] "running command" command="az storage container exists --name dacs-perf-2026-01-24t16-32-18z --account-name samplestorageacct --query exists -o tsv"
  I0124 16:32:22.208892  264576 utils.go:55] "running command" command="az storage container create --name dacs-perf-2026-01-24t16-32-18z --account-name samplestorageacct"
  I0124 16:32:26.574535  264576 utils.go:55] "running command" command="az storage account show --name samplestorageacct --query id -o tsv"
  I0124 16:32:29.491746  264576 utils.go:55] "running command" command="az ad sp show --id 00000000-0000-0000-0000-000000000000 --query id -o tsv"
  I0124 16:32:30.760613  264576 utils.go:55] "running command" command="az role assignment list --assignee-object-id 00000000-0000-0000-0000-000000000000 --scope /subscriptions/00000000-0000-0000-0000-000000000000/resourceGroups/sample-rg/providers/Microsoft.Storage/storageAccounts/samplestorageacct --role Storage Blob Data Contributor --query [].id -o tsv"
  STEP: Waiting for dacs-clients-4 job to complete @ 01/24/26 16:32:33.656
  I0124 16:32:33.657139  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:32:33.942633  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:32:39.237326  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:32:39.521316  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:32:44.794751  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:32:45.080576  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:32:50.355783  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:32:50.637793  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:32:55.913526  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:32:56.188670  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:33:01.467981  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:33:01.751357  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:33:07.046842  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:33:07.361499  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:33:12.661561  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:33:12.940425  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:33:18.228232  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:33:18.512408  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:33:23.796869  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:33:24.079911  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:33:29.355877  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:33:29.637992  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:33:34.919281  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:33:35.210707  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:33:40.510803  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:33:40.795538  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:33:46.078496  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:33:46.388930  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:33:51.662683  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:33:51.964256  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:33:57.249992  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:33:57.535776  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:34:02.811936  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:34:03.105854  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:34:08.394824  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:34:08.677854  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:34:13.970604  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:34:14.252309  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:34:19.539584  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:34:19.822648  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:34:25.096347  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:34:25.386016  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:34:30.675589  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:34:30.951832  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:34:36.228970  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:34:36.509615  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:34:41.791528  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:34:42.081639  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:34:47.373784  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:34:47.650901  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:34:52.939250  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:34:53.217362  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:34:58.497404  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:34:58.777547  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:35:04.087255  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:35:04.374369  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:35:09.659952  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:35:09.942524  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:35:15.208807  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:35:15.489679  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:35:20.772764  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:35:21.093709  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:35:26.392564  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:35:26.663091  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:35:31.950163  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:35:32.250580  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:35:37.537216  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:35:37.818400  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:35:43.095978  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:35:43.389140  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:35:48.677642  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:35:48.970091  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:35:54.242333  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:35:54.518858  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:35:59.793393  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:36:00.081175  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  I0124 16:36:05.368511  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Failed\")].status}"
  I0124 16:36:05.657092  264576 utils.go:55] "running command" command="kubectl get job dacs-clients-4 -n dacs-clients -o jsonpath={.status.conditions[?(@.type==\"Complete\")].status}"
  STEP: Parsing performance metrics from pod dacs-clients/dacs-clients-4-5xtq5 logs @ 01/24/26 16:36:05.989
  I0124 16:36:05.990374  264576 utils.go:55] "running command" command="kubectl logs dacs-clients-4-5xtq5 -n dacs-clients"
  I0124 16:36:07.147200  264576 utils.go:55] "running command" command="kubectl get node aks-manipool1-53964174-vmss000000 -o jsonpath={.metadata.labels.node\\.kubernetes\\.io/instance-type}"
  STEP: Parsing performance metrics from pod dacs-clients/dacs-clients-4-8wlhf logs @ 01/24/26 16:36:07.414
  I0124 16:36:07.415142  264576 utils.go:55] "running command" command="kubectl logs dacs-clients-4-8wlhf -n dacs-clients"
  I0124 16:36:08.455465  264576 utils.go:55] "running command" command="kubectl get node aks-manipool1-53964174-vmss000002 -o jsonpath={.metadata.labels.node\\.kubernetes\\.io/instance-type}"
  STEP: Parsing performance metrics from pod dacs-clients/dacs-clients-4-j4fz2 logs @ 01/24/26 16:36:08.739
  I0124 16:36:08.739429  264576 utils.go:55] "running command" command="kubectl logs dacs-clients-4-j4fz2 -n dacs-clients"
  I0124 16:36:09.810888  264576 utils.go:55] "running command" command="kubectl get node aks-manipool1-53964174-vmss000001 -o jsonpath={.metadata.labels.node\\.kubernetes\\.io/instance-type}"
  STEP: Parsing performance metrics from pod dacs-clients/dacs-clients-4-wrg9k logs @ 01/24/26 16:36:10.089
  I0124 16:36:10.089965  264576 utils.go:55] "running command" command="kubectl logs dacs-clients-4-wrg9k -n dacs-clients"
  I0124 16:36:11.295875  264576 utils.go:55] "running command" command="kubectl get node aks-manipool1-53964174-vmss000008 -o jsonpath={.metadata.labels.node\\.kubernetes\\.io/instance-type}"
  STEP: Aggregated Cache Client Performance Summary: TotalBlobsWritten: 200, TotalBytesWritten: 214748364800, TotalWriteTimeSeconds: 211.13, WriteThroughputGbps: 30.33, TotalBytesRead: 214748364800, TotalReadTimeSeconds: 252.00, ReadThroughputGbps: 25.43 @ 01/24/26 16:36:11.59
  STEP: Logging performance metrics @ 01/24/26 16:36:11.59
  I0124 16:36:11.590786  264576 utils.go:55] "running command" command="helm list -n cache-server -o json"
  I0124 16:36:12.181248  264576 kusto.go:162] "Perf metric" metric=<
        {
          "RunID": "",
          "SuiteID": "00000000-0000-0000-0000-000000000000",
          "Timestamp": "2026-01-24T16:36:12Z",
          "TestName": "Multiple DACS Clients E2E Tests Multiple DACS Clients Test Suite Should run multiple dacs cache clients performing writes",
          "TestGitCommit": "e3f5968cd34e53cc1dd57efaf460f67e2e48fd7a",
          "TestGitBranch": "sample/multicache-client",
          "HelmChartVersion": "cache-server-0.1.0",
          "HelmAppVersion": "1.16.0",
          "TestMetadata": {
            "StorageAccount": "samplestorageacct",
            "ContainerName": "dacs-perf-2026-01-24t16-32-18z",
            "ClusterName": "sample-cluster",
            "JobName": "dacs-clients-4",
            "IsMinikube": false,
            "WriteMode": "write_sync",
            "Clients": 4,
            "PodsMetadata": [
              {
                "PodName": "dacs-clients-4-5xtq5",
                "NodeName": "aks-manipool1-53964174-vmss000000",
                "NodeSKU": "Standard_L8as_v3",
                "PerfSummary": {
                  "BlobSize": 0,
                  "TotalBlobsWritten": 50,
                  "TotalBytesWritten": 53687091200,
                  "TotalWriteTimeSeconds": 54.9,
                  "WriteThroughputGbps": 7.286381797025932,
                  "TotalBytesRead": 53687091200,
                  "TotalReadTimeSeconds": 66.49,
                  "ReadThroughputGbps": 6.01550857528266
                }
              },
              {
                "PodName": "dacs-clients-4-8wlhf",
                "NodeName": "aks-manipool1-53964174-vmss000002",
                "NodeSKU": "Standard_L8as_v3",
                "PerfSummary": {
                  "BlobSize": 0,
                  "TotalBlobsWritten": 50,
                  "TotalBytesWritten": 53687091200,
                  "TotalWriteTimeSeconds": 51.46,
                  "WriteThroughputGbps": 7.772426612307109,
                  "TotalBytesRead": 53687091200,
                  "TotalReadTimeSeconds": 62.24,
                  "ReadThroughputGbps": 6.426567545553806
                }
              },
              {
                "PodName": "dacs-clients-4-j4fz2",
                "NodeName": "aks-manipool1-53964174-vmss000001",
                "NodeSKU": "Standard_L8as_v3",
                "PerfSummary": {
                  "BlobSize": 0,
                  "TotalBlobsWritten": 50,
                  "TotalBytesWritten": 53687091200,
                  "TotalWriteTimeSeconds": 52.29,
                  "WriteThroughputGbps": 7.649950609733157,
                  "TotalBytesRead": 53687091200,
                  "TotalReadTimeSeconds": 60.31,
                  "ReadThroughputGbps": 6.631936616657981
                }
              },
              {
                "PodName": "dacs-clients-4-wrg9k",
                "NodeName": "aks-manipool1-53964174-vmss000008",
                "NodeSKU": "Standard_L8as_v3",
                "PerfSummary": {
                  "BlobSize": 0,
                  "TotalBlobsWritten": 50,
                  "TotalBytesWritten": 53687091200,
                  "TotalWriteTimeSeconds": 52.48,
                  "WriteThroughputGbps": 7.621540634195236,
                  "TotalBytesRead": 53687091200,
                  "TotalReadTimeSeconds": 62.96,
                  "ReadThroughputGbps": 6.353031326702011
                }
              }
            ]
          },
          "Result": {
            "BlobSize": 0,
            "TotalBlobsWritten": 200,
            "TotalBytesWritten": 214748364800,
            "TotalWriteTimeSeconds": 211.13,
            "WriteThroughputGbps": 30.330299653261434,
            "TotalBytesRead": 214748364800,
            "TotalReadTimeSeconds": 252,
            "ReadThroughputGbps": 25.42704406419646
          }
        }
   >
  I0124 16:36:12.183096  264576 kusto.go:166] "SEND_METRICS not set to 'true', skipping EventHub send"
  I0124 16:36:12.183286  264576 utils.go:55] "running command" command="az storage container delete --name dacs-perf-2026-01-24t16-32-18z --account-name samplestorageacct"
  I0124 16:36:15.745058  264576 utils.go:55] "running command" command="kubectl delete ns dacs-clients --ignore-not-found --wait"
• [244.098 seconds]
------------------------------
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