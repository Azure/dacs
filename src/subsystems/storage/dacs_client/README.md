# DACS Client

Refer Wiki for more details - [DACS Client Wiki](internal-reference-removed)

## DACS Client Library

This library provides interfaces to perform file operations with caching mechanisms for file handles. It includes both basic and managed operations to handle file reads and reference counting for file handles.

### Usage
1. Create an instance of `DACSCacheClient()` to use the library (no parameters needed).
2. Set up authentication via environment variables (see Authentication section below).
3. Use the instance to perform the following file operations.

### Authentication
The client supports two authentication modes:

**Mode 1: SAS Token (Development)**
- Set `STORAGE_INTCPT_SAS_TOKEN` environment variable with your SAS token
- Example: `export STORAGE_INTCPT_SAS_TOKEN="sv=2021-06-08&..."`

**Mode 2: Managed Identity (Production - Default)**
- Set `AZURE_CLIENT_ID` environment variable
- Optionally override `MSI_ENDPOINT` (defaults to IMDS: `http://169.254.169.254/metadata/identity/oauth2/token`)
- Optionally override `MSI_SECRET` (defaults to `dummy` for IMDS)
- Example: `export AZURE_CLIENT_ID="your-client-id"`

### Basic Operations
- **`get_file_size(blob_path)`**:
  Retrieve the size of a blob.

- **`read_blob(blob_path, buffer, size, offset)`**:
  Read data from a file using managed interfaces; users do not need to manually close the file.

- **`write_blob(blob_path, buffer, size, offset)`**:
  Write data to a file using managed interfaces; users do not need to manually close the file.

- **`list_dir(blob_dir_path)`**:
  List all blobs in a specified directory.

- **`shutdown()`**:
  Close all open file handles and release resources.

### Notes
- This library uses the StorageIntercept's Wrapper for file operations.
- Ensure proper resource management by invoking `shutdown()` after completing file operations.

---

## DACS Client Library Utils

This module provides utility functions to enhance the functionality of the DACS Client. These utilities help manage memory buffers and configure access settings for seamless integration with Azure Blob Storage.

### Utility Functions

- **`alloc_buffer(size)`**:
  Allocates a memory buffer of the specified size for reading operations.

- **`release_buffer(buffer)`**:
  Releases the allocated memory buffer and logs the memory deallocation.

### Notes

- The client uses dynamic storage accounts, so no storage account name is required during initialization.
- Authentication is configurable via environment variables for maximum flexibility.
- Ensure proper resource management by invoking `shutdown()` after completing file operations.
- The StorageIntercept configuration is automatically created with sensible defaults.

---

## DACS Client Library Wrappers

The library provides wrapper functions to simplify complex operations, such as downloading all remote blobs or files to a local disk.

### Wrapper Functions

- **`download_remote_blobs_to_disk(remote_blob_uri, local_disk_path)`**:
  Downloads remote blobs from the specified `remote_blob_uri` and writes them to the local disk at `local_disk_path`. This function internally creates and manages a DACS Client instance for the operation. Authentication is handled automatically via environment variables.


# For Dev Operations

# To Build
```
docker run -it --entrypoint /bin/bash -v /mnt/c/CodeBase/DACS/src/subsystems/storage:/storage mcr.microsoft.com/vscode/devcontainers/python:3.11

#Install Azure CLI and Login, if below setup.py fails
#curl -sL https://aka.ms/InstallAzureCLIDeb | sudo bash
#az login

cd /storage/dacs_client
python setup.py bdist_wheel
ls -al dist/
```

# To Publish
```
To make a release with updated version, ensure to change VERSION in setup.py.
    /src/subsystems/storage/dacs_client/setup.py

This would help to generate the whl with version starting as "{distribution}-{version}.(-{build tag})?"

Configure the .pypirc file
    [distutils]
    Index-servers = Storage-Cache-Feed

    [Storage-Cache-Feed]
    Repository = internal-reference-removed
    Username = <feed-username>
    Password = <PAT>

cd /storage/dacs_client/
python3 -m pip install --upgrade twine
python3 -m twine upload --repository Storage-Cache-Feed dist/* --config ./.pypirc
```

# To Test
```
Set authentication (choose one method):

For SAS Token (Development):
export STORAGE_INTCPT_SAS_TOKEN="your-sas-token"

For Managed Identity (Production):
export AZURE_CLIENT_ID="your-client-id"
# Optional: MSI_ENDPOINT and MSI_SECRET default to IMDS values
export MSI_ENDPOINT="http://169.254.169.254/metadata/identity/oauth2/token"
export MSI_SECRET="dummy"

Run the setup script (if needed):
sh /usr/local/lib/python3.11/site-packages/dacs_client/setup.sh

Validate installation:
root ➜ /storage/2020-12-01-preview/_generated $ python
Python 3.11.11 (main, Dec  4 2024, 20:38:25) [GCC 12.2.0] on linux
Type "help", "copyright", "credits" or "license" for more information.
>>> from py_dacs_client import dacs_client
>>> dacs_client.help()
'\n    DACS Client library exposes interfaces to perform file operations.\n    '
>>>

Run a test job:
/storage/2020-12-01-preview/_generated/dacs_client/sample $ python dacs_client_test.py
```