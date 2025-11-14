# DACS Cache Client

Refer Wiki for more details - [DACS Cache Client Wiki](internal-reference-removed)

## DACS Cache Client Library

This library provides interfaces to perform file operations with caching mechanisms for file handles. It includes both basic and managed operations to handle file reads and reference counting for file handles.

### Usage
1. Create an instance of `DACSCacheClient` to use the library.
2. Use the instance to perform the following file operations.

### Basic Operations
- **`get_file_size(blob_path)`**:
  Retrieve the size of a blob.

- **`read_blob(blob_path, buffer, size, offset)`**:
  Read data from a file using managed interfaces; users do not need to manually close the file.

- **`list_dir(blob_dir_path)`**:
  List all blobs in a specified directory.

- **`shutdown()`**:
  Close all open file handles and release resources.

### Notes
- This library uses the StorageIntercept's Wrapper for file operations.
- Ensure proper resource management by invoking `shutdown()` after completing file operations.

---

## DACS Cache Client Library Utils

This module provides utility functions to enhance the functionality of the DACS Cache Client. These utilities help manage memory buffers and configure access settings for seamless integration with Azure Blob Storage.

### Utility Functions

- **`get_buffer_to_read(size)`**:
  Allocates a memory buffer of the specified size for reading operations.

- **`release_buffer(buffer)`**:
  Releases the allocated memory buffer and logs the memory deallocation.

- **`configure_managed_identity_mode(azure_client_id)`**:
  Configures the managed identity mode for obtaining blob access tokens using the specified Azure client ID.

- **`initialize_dacs_cache_client_config(intercept_path, sas_token)`**:
  Initialize the DACS Cache Client with the given intercept path and SAS token and if IB mode is to be enabled

### Notes

- Ensure that the `sas_token` is valid and has the required permissions for accessing the blobs.
- Use the utility functions to simplify memory management and configuration tasks when working with the DACS Cache Client.

---

## DACS Cache Client Library Wrappers

The library provides wrapper functions to simplify complex operations, such as downloading all remote blobs or files to a local disk.

### Wrapper Functions

- **`download_remote_blobs_to_disk(storage_sas_token, remote_blob_uri, local_disk_path)`**:
  Downloads remote blobs from the specified `remote_blob_uri` using the provided `storage_sas_token` and writes them to the local disk at `local_disk_path`. This function internally creates and manages a DACS Cache Client instance for the operation.


# For Dev Operations

# To Build
```
docker run -it --entrypoint /bin/bash -v /mnt/c/CodeBase/DACS/src/sdk/Microsoft.DACS/storage:/storage -v /mnt/c/CodeBase/DACS/obj/Debug/native/linux-x64/publish-storage/storage:/storagepkg mcr.microsoft.com/vscode/devcontainers/python:3.11

#Install Azure CLI and Login, if below setup.py fails
#curl -sL https://aka.ms/InstallAzureCLIDeb | sudo bash
#az login

cd /storage/2020-12-01-preview/_generated
python setup.py bdist_wheel
ls -al dist/
```

# To Publish
```
To make a release with updated version, ensure to change VERSION in below two files.
    /src/sdk/Microsoft.DACS/storage/2020-12-01-preview/_build_generated.ps1
    /src/sdk/Microsoft.DACS/storage/2020-12-01-preview/_generated/setup.py

This would help to generate the whl generated starting as "{distribution}-{version}.(-{build tag})?"

Configure the .pypirc file
    [distutils]
    Index-servers = DACS-Deps-Official

    [DACS-Deps-Official]
    Repository = internal-reference-removed

cd dacs_cache/
python3 -m pip install --upgrade twine
python3 -m twine upload --repository  DACS-Deps-Official dist/* --config ./.pypirc

az artifacts universal publish --organization internal-reference-removed --feed DACS-Deps-Official --name dacs_cache --version 0.0.2.0 --description "Python package for DACS Cache Client" --path dacs_cache-0.0.2.0-py3-none-any.whl
```

# To Test
```
python3 -m pip install dist/dacs_cache-0.0.2.0-py3-none-any.whl --force-reinstall --no-deps dacs-client
    OR
python3 -m pip install -i internal-reference-removed dacs-client
    NB: Give the username and PAT for the feed

Run the setup script
sh /usr/local/lib/python3.11/site-packages/py_dacs_cache_client/setup.sh

To Test
Set the SAS Token
export STORAGE_INTCPT_SAS_TOKEN="SAS TOKEN"

For Dev
python3 -m pip install requests
cd /storage/2020-12-01-preview/_generated/;
rm -rf dist;python setup.py bdist_wheel;ls -al dist;
python3 -m pip install dist/dacs_cache-0.0.*-py3-none-any.whl --force-reinstall --no-deps dacs-client;

# Validate Once if installed
root ➜ /storage/2020-12-01-preview/_generated $ python
Python 3.11.11 (main, Dec  4 2024, 20:38:25) [GCC 12.2.0] on linux
Type "help", "copyright", "credits" or "license" for more information.
>>> from py_dacs_cache_client import dacs_cache_client
>>> dacs_cache_client.help()
'\n    DACS Cache Client library exposes interfaces to perform file operations.\n    '
>>>

Or run a test job
/storage/2020-12-01-preview/_generated/py_dacs_cache_client/sample $ python dacs_cache_client_test.py
```