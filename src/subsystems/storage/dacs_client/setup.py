from datetime import datetime
import os
from setuptools import setup
import shutil

# VERSION is a placeholder that gets replaced at build time by build-python-package.sh
# The actual version (e.g., 0.9.0.202601148) is SemVer format: 0.9.0.YYYYMMDDN
# Do not change this placeholder value - it's used by sed to find and replace
VERSION = '0.9.0-PLACEHOLDER'

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
        build_arch = os.environ.get('BUILD_ARCHITECTURE')
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
            arch_dir_name = build_arch

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

# Get architecture and channel from environment (both required)
build_arch = os.environ.get('BUILD_ARCHITECTURE')
build_channel = os.environ.get('BUILD_CHANNEL')

# Validate required environment variables
if not build_arch or not build_channel:
    missing = []
    if not build_arch:
        missing.append("BUILD_ARCHITECTURE (e.g., 'amd64' or 'arm64')")
    if not build_channel:
        missing.append("BUILD_CHANNEL (e.g., 'official' or 'dev')")
    raise ValueError(f"Missing required environment variables: {', '.join(missing)}")

# Map architecture to proper wheel platform tags
platform_tag_map = {
    'amd64': 'linux_x86_64',
    'arm64': 'linux_aarch64'
}
platform_tag = platform_tag_map.get(build_arch)
if not platform_tag:
    raise ValueError(f"Unknown architecture: {build_arch}. Expected 'amd64' or 'arm64'.")

setup(
    name=f'dacs-client-{build_channel}',
    version=VERSION,
    packages=[
        "py_dacs_client",
        "py_dacs_client.utils",
        "py_dacs_client.tests",
        "py_dacs_client.benchmarks",
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
    options={
        'bdist_wheel': {
            'plat_name': platform_tag
        }
    }
)
