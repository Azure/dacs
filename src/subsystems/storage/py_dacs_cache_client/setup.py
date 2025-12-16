from datetime import datetime
import os
from setuptools import setup
import shutil

VERSION = '1.2.0'

setup_dir = os.path.abspath(os.path.dirname(__file__))
print("Current working directory:", setup_dir)

CLASSIFIERS = [
    'Development Status :: 5 - Production/Stable',
    'Intended Audience :: Developers',
    'Intended Audience :: Science/Research',
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
        
        # Get architecture from environment variable
        build_arch = os.environ.get('BUILD_ARCHITECTURE', 'linux-x64')
        print(f"Building for architecture: {build_arch}")
        
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
            so_files_source_direct = f'{setup_dir}/storage/libStorageDirect.so'
            
            # Determine architecture for bin path
            arch_for_bin = 'amd64' if 'x64' in build_arch else 'arm64'
            
            # bin/<config>/<arch>/native-storage/storage/
            so_new_repo_release = f'{setup_dir}/../../../../bin/Release/{arch_for_bin}/native-storage/storage/libStorageIntercept.so'
            so_new_repo_debug = f'{setup_dir}/../../../../bin/Debug/{arch_for_bin}/native-storage/storage/libStorageIntercept.so'
            so_new_repo_release_direct = f'{setup_dir}/../../../../bin/Release/{arch_for_bin}/native-storage/storage/libStorageDirect.so'
            so_new_repo_debug_direct = f'{setup_dir}/../../../../bin/Debug/{arch_for_bin}/native-storage/storage/libStorageDirect.so'

            # Copy libStorageIntercept.so
            if os.path.isfile(so_files_source):
                shutil.copy(so_files_source, current_build_folder)
                print(f"Found & Copied {so_files_source} to {current_build_folder}")
            elif os.path.isfile(so_new_repo_release):
                shutil.copy(so_new_repo_release, current_build_folder)
                print(f"New Repo (Release): Found & Copied {so_new_repo_release} to {current_build_folder}")
            elif os.path.isfile(so_new_repo_debug):
                shutil.copy(so_new_repo_debug, current_build_folder)
                print(f"New Repo (Debug): Found & Copied {so_new_repo_debug} to {current_build_folder}")
            else:
                print(f"Warning !!! libStorageIntercept.so not found in:")
                print(f"  - {so_files_source}")
                print(f"  - {so_new_repo_release}")
                print(f"  - {so_new_repo_debug}")
                raise FileNotFoundError(f"libStorageIntercept.so not found in any expected location")

            # Copy libStorageDirect.so
            if os.path.isfile(so_files_source_direct):
                shutil.copy(so_files_source_direct, current_build_folder)
                print(f"Found & Copied {so_files_source_direct} to {current_build_folder}")
            elif os.path.isfile(so_new_repo_release_direct):
                shutil.copy(so_new_repo_release_direct, current_build_folder)
                print(f"New Repo (Release): Found & Copied {so_new_repo_release_direct} to {current_build_folder}")
            elif os.path.isfile(so_new_repo_debug_direct):
                shutil.copy(so_new_repo_debug_direct, current_build_folder)
                print(f"New Repo (Debug): Found & Copied {so_new_repo_debug_direct} to {current_build_folder}")
            else:
                print(f"Warning !!! libStorageDirect.so not found in:")
                print(f"  - {so_files_source_direct}")
                print(f"  - {so_new_repo_release_direct}")
                print(f"  - {so_new_repo_debug_direct}")
                # Commenting out the exception raise for libStorageDirect.so for now
                # raise FileNotFoundError(f"libStorageDirect.so not found in any expected location")
            print('\n>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> End custom packaging logic')
        install.run(self)

with open(os.path.join(setup_dir, 'README.md'), encoding='utf-8') as f:
    long_description = f.read()

# Get architecture from environment to make package name unique
build_arch = os.environ.get('BUILD_ARCHITECTURE', 'linux-x64')
arch_suffix = build_arch.replace('-', '_')  # Convert linux-x64 to linux_x64 for valid package name

setup(
    name=f'dacs-client-{arch_suffix}',
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
