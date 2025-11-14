from datetime import datetime
import os
from setuptools import setup
import shutil

# While changing the version, please update the version in the following file as well:
# - src/sdk/Microsoft.DACS/storage/2020-12-01-preview/_build_generated.ps1
VERSION = '1.2.0'

setup_dir = os.path.abspath(os.path.dirname(__file__))
print("Current working directory:", setup_dir)

CLASSIFIERS = [
    'Development Status :: 5 - Production/Stable',
    'Intended Audience :: Developers',
    'Intended Audience :: Science/Research',
    'License :: Other/Proprietary License',
    'Programming Language :: Python',
    'Programming Language :: Python :: 3 :: Only',
    'Programming Language :: Python :: 3.7',
    'Programming Language :: Python :: 3.8',
    'Programming Language :: Python :: 3.9',
    'Programming Language :: Python :: 3.10',
    'Programming Language :: Python :: 3.11',
    'Operating System :: MacOS',
    'Operating System :: Microsoft :: Windows',
    'Operating System :: POSIX :: Linux',
    'Topic :: Scientific/Engineering'
]

from setuptools.command.install import install
class CustomPackageLogic(install):
    def run(self):
        # Packing the required dependencies into the whl package
        print('\n>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> Start custom packaging logic')
        entry_file_path = None
        for root, _, files in os.walk(setup_dir):
            if 'configure.py' in files:
                entry_file_path = os.path.join(root, 'configure.py')
                break
        if entry_file_path:
            print("Full path of configure.py:", entry_file_path)
            current_build_folder = os.path.dirname(entry_file_path)
            print("Current Build folder:", current_build_folder)

            # List the contents of {setup_dir}/storage
            storage_dir = os.path.join(setup_dir, 'storage')
            if os.path.isdir(storage_dir):
                print("Initial Contents of storage directory:")
                for item in os.listdir(storage_dir):
                    print(item)
            else:
                print(f"{storage_dir} directory does not exist")

            so_files_source = f'{setup_dir}/storage/libStorageIntercept.so'
            # For Pipeline: Check if the required library is in some path C:\__w\1\s\bin\Release\storage\publish\az-dacs-storage-official\storage\libStorageIntercept.so
            so_pipeline_files_source = f'{setup_dir}/../../../../../../bin/Release/storage/publish/az-dacs-storage-official/storage/libStorageIntercept.so'

            if os.path.isfile(so_files_source):
                shutil.copy(so_files_source, current_build_folder)
                print(f"Found & Copied {so_files_source} to {current_build_folder}")
            elif os.path.isfile(so_pipeline_files_source):
                shutil.copy(so_pipeline_files_source, current_build_folder)
                print(f"Pipeline: Found & Copied {so_pipeline_files_source} to {current_build_folder}")
            else:
                print(f"libStorageIntercept.so not found in {setup_dir}/storage and {so_pipeline_files_source}")
                raise FileNotFoundError(f"libStorageIntercept.so not found in {setup_dir} and {so_pipeline_files_source}")
            print('\n>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> End custom packaging logic')
        install.run(self)

with open(os.path.join(setup_dir, 'README.md'), encoding='utf-8') as f:
    long_description = f.read()

setup(
    name='dacs-client',
    version=VERSION,
    packages=[
        "py_dacs_cache_client",
        "py_dacs_cache_client.utils",
        "py_dacs_cache_client.sample",
    ],
    install_requires=[
        # Need add only the python package dependencies here, if any
    ],
    author='Microsoft Corporation',
    author_email='dacs-storage@users.noreply.github.com',
    url='internal-reference-removed
    description='Python package for DACS Cache Client',
    long_description=long_description,
    long_description_content_type='text/markdown',
    classifiers=CLASSIFIERS,
    python_requires='>=3.8',
    cmdclass={'install': CustomPackageLogic},
)
