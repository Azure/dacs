
# Copyright (c) Microsoft Corporation.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import os
import shutil

from setuptools import setup
from setuptools.command.install import install

setup_dir = os.path.abspath(os.path.dirname(__file__))
print("Current working directory:", setup_dir)



class CustomPackageLogic(install):
    def run(self):
        # Packing the required dependencies into the whl package
        print('\n>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> Start custom packaging logic')

        # Get architecture from environment variable
        build_arch = os.environ.get('BUILD_ARCHITECTURE')
        print(f"Building for architecture: {build_arch}")

        # Destination is the package dir under self.build_lib so install_lib
        # picks it up when assembling the wheel.
        current_build_folder = os.path.join(self.build_lib, 'py_dacs_client')
        os.makedirs(current_build_folder, exist_ok=True)
        print("Current Build folder:", current_build_folder)

        # For dev machine: Check if the library is in storage/libStorageDirect.so
        storage_dir = os.path.abspath(os.path.join(setup_dir, '../../../..', 'bin/Debug/native-storage/storage'))
        libstorage_direct_dev = f'{storage_dir}/libStorageDirect.so'
        # For new repo: Check if the library is in bin/<config>/<arch>/native-storage/storage
        libstorage_direct_release = f'{setup_dir}/../../../../bin/Release/{build_arch}/native-storage/storage/libStorageDirect.so'
        libstorage_direct_debug = f'{setup_dir}/../../../../bin/Debug/{build_arch}/native-storage/storage/libStorageDirect.so'

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


setup(cmdclass={'install': CustomPackageLogic})
