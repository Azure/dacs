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

"""Shared Azure AD auth helper for workload methods that use libStorageDirect.so.

The native library tries IMDS-based auth which fails on workload identity
clusters (HTTP 400).  This module fetches an Azure AD token via
DefaultAzureCredential and injects it into SI_azBlobAccessToken so the
native library skips IMDS.
"""

import os

_auth_done = False


def ensure_auth():
    """Inject Azure AD access token into SI_azBlobAccessToken if not already set."""
    global _auth_done
    if _auth_done:
        return

    if not os.environ.get("SI_azBlobAccessToken"):
        try:
            from azure.identity import DefaultAzureCredential
            cred = DefaultAzureCredential()
            token = cred.get_token("https://storage.azure.com/.default")
            os.environ["SI_azBlobAccessToken"] = token.token
            _auth_done = True
            print("  Injected Azure AD access token for native lib", flush=True)
        except Exception as e:
            print(f"  Warning: could not fetch access token: {e}", flush=True)
    else:
        _auth_done = True
