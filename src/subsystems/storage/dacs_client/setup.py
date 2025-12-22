from datetime import datetime
import os
from setuptools import setup
import shutil

# While updating, ensure to update in build-python-package.sh as well.
VERSION = '0.9.0'

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
    'Programming Language :: Python :: 3.12',
    'Programming Language :: Python :: 3.13',
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
        # Look for configure.py only in build/lib directory
        build_lib_dir = os.path.join(setup_dir, 'build', 'lib')
        for root, _, files in os.walk(build_lib_dir):
            if 'configure.py' in files:
                entry_file_path = os.path.join(root, 'configure.py')
                break
        if entry_file_path:
            print("Full path of configure.py:", entry_file_path)
            current_build_folder = os.path.dirname(entry_file_path)
            print("Current Build folder:", current_build_folder)

            # List the contents of storage directory
            storage_dir = os.path.join(setup_dir, '../../../..', 'bin/Debug/native-storage/storage')
            storage_dir = os.path.abspath(storage_dir)
            print(f"Storage directory path: {storage_dir}")
            if os.path.isdir(storage_dir):
                print("Initial Contents of storage directory:")
                for item in os.listdir(storage_dir):
                    print(item)
            else:
                print(f"{storage_dir} directory does not exist")

            # Determine architecture for bin path
            arch_dir_name = 'amd64' if 'x64' in build_arch else 'arm64'

            # For dev machine: Check if the library is in storage/libStorageDirect.so
            libstorage_direct_dev = f'{storage_dir}/libStorageDirect.so'
            # For new repo: Check if the library is in bin/<config>/<arch>/native-storage/storage
            libstorage_direct_release = f'{setup_dir}/../../../../bin/Release/{arch_dir_name}/native-storage/storage/libStorageDirect.so'
            libstorage_direct_debug = f'{setup_dir}/../../../../bin/Debug/{arch_dir_name}/native-storage/storage/libStorageDirect.so'

            # Copy libStorageDirect.so
            if os.path.isfile(libstorage_direct_dev):
                shutil.copy(libstorage_direct_dev, current_build_folder)
                print(f"Found & Copied {libstorage_direct_dev} to {current_build_folder}")
            elif os.path.isfile(libstorage_direct_release):
                shutil.copy(libstorage_direct_release, current_build_folder)
                print(f"New Repo (Release): Found & Copied {libstorage_direct_release} to {current_build_folder}")
            elif os.path.isfile(libstorage_direct_debug):
                shutil.copy(libstorage_direct_debug, current_build_folder)
                print(f"New Repo (Debug): Found & Copied {libstorage_direct_debug} to {current_build_folder}")
            else:
                print(f"Warning !!! libStorageDirect.so not found in:")
                print(f"  - {libstorage_direct_dev}")
                print(f"  - {libstorage_direct_release}")
                print(f"  - {libstorage_direct_debug}")
                raise FileNotFoundError(f"libStorageDirect.so not found in any expected location")
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
        "py_dacs_client",
        "py_dacs_client.utils",
        "py_dacs_client.sample",
        "py_dacs_client.tests",
    ],
    package_data={
        "py_dacs_client.tests": ["requirements.txt"],
    },
    install_requires=[
        "blobfile>=3.0.0",
        "azure-identity>=1.12.0",
    ],
    author='Microsoft Corporation',
    author_email='dacs-storage@users.noreply.github.com',
    url='internal-reference-removed
    description='Python package for DACS Client with blobfile-compatible API',
    long_description=long_description,
    long_description_content_type='text/markdown',
    classifiers=CLASSIFIERS,
    python_requires='>=3.8',
    cmdclass={'install': CustomPackageLogic},
)
