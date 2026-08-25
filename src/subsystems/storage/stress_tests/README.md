# Kubernetes stress testing framework

## Overview
A stress testing framework for cacheserver/client interactions on kubernetes based clusters. This sub-project is organized as follows:

> **Important:** This framework is not ready to run externally without
> configuration. Before running a test, publish the images you built to a
> registry accessible from the cluster. Set `CACHE_SERVER_IMAGE` for the Linux
> runner, update the client image in `k8s/stress-client.template.yaml`, and
> provide real storage account settings in the selected scenario file. Inspect
> any additional manifest you apply for its own `image:` fields.

1. _container/_ - contains a legacy standalone container definition. Build the
    current stress and performance client image through the supported
    repository flow:
    ```bash
    REPOSITORY_ROOT=/path/to/dacs
    cd "$REPOSITORY_ROOT"
    make build BUILD_TYPE=Release
    make images BUILD_TYPE=Release
    ```
    Run both commands from the repository root. The image build creates
    `cache-test-client:local`, which contains the stress clients and
    `PerfClient`. Publish or retag that image using your registry's supported
    workflow before updating the manifests described above.
2. _k8s_ - contains the automation scripts, test scenario configurations and kubernetes yaml templates used to launch the stress tests. More details on this later.
3. _launcher.py_ - Entrypoint for the stress testing client container image. The launcher is responsible for starting up one or many client programs    that read from a specified dataset. That is, each container image launched executes the launcher script, which spawns one or more client programs of the same kind, each processing the same dataset. The launcher accepts four arguments:
    * **--storage_path**: This is the storagePath that the storage interceptor config expects. Any IO performed on this path gets intercepted by the interceptor library.
    * **--name**: This is the name of the dataset that the clients launched should process. This must correspond to a valid blob directory inside the storage container that was specified for use (storage container is specified via test scenario configuration file, more on that inside the k8s directory).
    * **--num_workers**: This is the number of client programs that should be spawned. Each client program reads the same dataset.
    * **--client_type**: Currently supports three clients -
        * **cpp**: Multithreaded C++ client that lists all files in the dataset and hands off files to read by individual worker threads.
        * **python**: Python client that lists all files in the dataset and hands off files to read by individual worker processes.
        * **delete**: Cpp client that issues an RPC call to all listed cache servers to delete all keys with a specified group id.
4. _Client programs_: StorageClient.py StorageClient.cpp DeleteClient.cpp implement the various supported client types.
5. _storageIntercept.config_: The default interception config that each client process would use. This is shipped with the container image via building with the dockerfile.


## Deep dive into the testing framework
Currently, we support running the test framework on Linux and Windows platforms. There are corresponding bash and powershell scripts that automate the test execution. The scripts are located in the k8s directory.

### Pre-requisites
For both Windows and Linux, configure a kubeconfig for the target Kubernetes
cluster before running the scripts. Consult the documentation for your
Kubernetes provider to obtain credentials.

#### Common Setup
1. Ensure that kubectl is installed in your system, and present in your PATH environment variable.

#### For Linux/WSL setup
1. Install ```jq```:
```
sudo apt-get install jq
```

#### For Windows setup
1. Make sure that your powershell has the ```Az``` module installed:
```
Install-Module -Name Az -Repository PSGallery -Force
```

### Running a stress test
#### Linux/WSL
```bash
REPOSITORY_ROOT=/path/to/dacs
cd "$REPOSITORY_ROOT/src/subsystems/storage/stress_tests/k8s"
CACHE_SERVER_IMAGE=registry.example/cache-server:tag \
  ./launcher.sh /path/to/kubeconfig /path/to/scenario.json
```
#### Windows
```powershell
Set-Location C:\path\to\dacs\src\subsystems\storage\stress_tests\k8s
.\AutomateTests.ps1 -KubeConfig C:\path\to\kubeconfig -OutputDir C:\path\to\output -TestConfigFile C:\path\to\scenario.json
```

### Explanation:
The test framework provides a way for running test scenarios - iteratively launching client pods on the kubernetes cluster that will read from a remote dataset, and in the process, fetch data from the storage cache server. In addition to launching clients automatically, it is also responsible for collecting, validating and comparing the test output from the client pods to verify correct execution of the test. The test scenario runs indefinitely, unless explicity stopped, launching sets of clients over and over, marking one iteration of the test as complete when all clients have finished execution. When one iteration completes, and if explicitly specified, the test framework waits for a while before beginning the next iteration. A test scenario is defined by a json config file (see sample below).

#### End-to-end flow:
Regardless of platform, the end-to-end flow of the test framework is nearly identical. The only differences between the Bash/Powershell scripts is that the Bash setup will also try to spawn a cacheserver service and pods on the kubernetes cluster. With the Powershell setup, we assume that a cluster with cacheserver pods are already running. Additionally, the Bash scripts have the ability to simulate a server crash by deleting individual server pods. However, this may not prove helpful for server restart testing since deleting the pod takes down its associated ephemeral storage. The end-to-end flow is as follows:


1. Create an output directory where the generated kubernetes yaml files, as well as client/server pods logs will be stored.
2. Generate the kubernetes yaml files for the test scenario. A templatized client/server yaml file is used. For each of the client pods that the test should spawn, a new yaml file is generated from the template with the appropriate parameters populated from the test scenario configuration file. You can find a sample json configuration inside the ```k8s/scenarios/``` folder.Here are the list of currently supported test scenario configuration parameters:
* **TestName**: Name of the test scenario.
* **AccountName**: Storage account where the dataset is hosted.
* **AccountEndpoint**: Endpoint of the storage account where the dataset is hosted.
* **ContainerName**: Container inside storage account that holds the dataset to be read.
* **SASToken**: SAS token for the storage account.
* **ServerConfig**(**_Bash only_**):
    * Replicas
    * CacheSize
    * EphemeralLimit
    * Crash interval (iterations between server crash simulations, requires sequential test to be false)
* **SequentialTest** (**_Bash only_**): If true, the test framework will wait for one set of clients to finish execution before launching the next set in the same iteration. If false, the test framework will launch all sets of clients at once, and wait for all of them to finish execution before marking the iteration as complete.
* **SleepInterval**: The amount of time to wait between iterations of the test.
* **ClientConfigs**: A list of client configs, each entry in the list refers to a single set of clients. Each client config has the following parameters:
    * Name: Name of the client set.
    * Parallelism: Number of clients to launch in parallel.
    * ServerHostnames: Hash separated list of server hostnames to connect to.
    * KeyExpiry: Expiry time for the keys that the clients will read.
    * Dataset: Name of the dataset that the clients will read.
    * ClientType: Type of client to launch. Currently supports cpp, python and delete.
    * IOVerification: If true, the client will verify the number of bytes by the client matches the dataset size present in the storage container. 
    * GroupId: The cache group id that will be used for inserting/deleting keys to/from the cache.


#### Linux flow
Entrypoint to the test framework is ```launcher.sh``` which in turn invokes ```main.sh```. The main script is where the main test execution logic resides, sourcing individual functions from the ```utils.sh``` file. The separation between ```launcher.sh``` and ```main.sh``` is required to correctly handle tee'ing output and trapping interrupt signal concurrently.

#### Windows flow
Entrypoint to the test framework is ```AutomateTests.ps1```. All logic is present inside this file.

### Adding new test scenarios
Create a new configuration file that mimics the existing template present in the ```k8s/scenarios``` folder.

## Additional notes
### Delete client
The client container image supports the _DeleteClient_, which basically issues an RPC call to delete all keys with a specified group id. However, this hasn't been tested completely yet. This can be enabled by setting the client config appropriately. For example:
```
{
    "Name": "deletejob",
    "Parallelism": 1,
    "ServerHostnames": "cacheserver-0.cacheserver.default.svc.cluster.local",
    "KeyExpiry": 0, # Ignored for delete client
    "Dataset": "books_wiki_en_corpus", # Ignored for delete client
    "ClientType": "delete",
    "IOVerification": false, # Must be set to false for delete client
    "GroupId": "group1" # The cache group id that will be used in the RPC call for delete
}
```

### Configuring timeouts
At various places in the test framework, timeouts are used to ensure that a test doesn't go on indefinitely. These timeouts can be changed by modifying the ```utils.sh``` file for Linux, and ```AutomateTests.ps1``` for Windows. The following functions are relevant:

**Powershell**
```
Test-PodsRunning
Test-PodsCompleted
```

**Bash**
```
_check_pods_running_
_check_pods_completed_
```

### Setting storage interception configuration parameters
Storage interceptor supports overriding default configuration parameters by setting environment variables. You can add any override you so desire inside the stress-client.template.yaml file. See the file for sample overrides. Note that the environment variable needs to follow the nomenclature ```SI_<parameterName>```. The parameters name is case insensitive, but ```SI``` must be allcaps.