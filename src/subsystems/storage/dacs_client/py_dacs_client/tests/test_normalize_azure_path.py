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

"""Normalization only; native endpoint policy is covered by C++ tests.

Run with --noconftest to avoid unrelated Azure integration cleanup.
"""

import importlib.util
import sys
from types import ModuleType
from unittest.mock import Mock

import pytest


@pytest.fixture
def client_module(monkeypatch):
    loader = ModuleType("py_dacs_client.dacs_data_loader")
    loader.DACSCacheDataLoader = Mock()
    loader.Stat64 = Mock()
    monkeypatch.setitem(sys.modules, loader.__name__, loader)
    source = importlib.util.find_spec("py_dacs_client.dacs_client").origin
    spec = importlib.util.spec_from_file_location("_dacs_path_tests", source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@pytest.mark.parametrize("host", [
    "account.blob.core.windows.net",
    "Account.Blob.Core.ChinaCloudApi.CN",
    "account.blob.core.usgovcloudapi.net",
    "account.blob.example.test",
])
@pytest.mark.parametrize("scheme", ["https", "az"])
@pytest.mark.parametrize("remainder", [
    "", "/", "/container", "/container/", "/container/directory/model.bin",
    "/container/a%2Fb%20c+name?sv=2021-06-08&sig=example%2Bsignature%3D#fragment",
])
def test_full_endpoint(client_module, host, scheme, remainder):
    path = f"{scheme}://{host}{remainder}"
    expected = f"az://{host}{remainder}"
    if scheme == "https" and not remainder:
        expected += "/"
    assert client_module.normalize_azure_path(path) == expected
    assert client_module.normalize_azure_path(expected) == expected


@pytest.mark.parametrize("remainder", ["", "/", "/container", "/container/path/blob"])
def test_short_form_keeps_global_default(client_module, monkeypatch, remainder):
    monkeypatch.setenv("AZURE_BLOB_ENDPOINT_SUFFIX", "blob.example.test")
    assert client_module.normalize_azure_path(f"az://account{remainder}") == (
        f"az://account.blob.core.windows.net/{remainder.lstrip('/')}"
    )


@pytest.mark.parametrize("path", [
    "", "/container/blob", "http://account/container/blob", "s3://account/container/blob",
    "az:///container/blob", "https:///container/blob", "az://./container/blob",
    "az://../container/blob", "https://../container/blob",
])
def test_invalid_path_shape(client_module, path):
    with pytest.raises(ValueError):
        client_module.normalize_azure_path(path)


@pytest.mark.parametrize("host", [
    "unknown.example",
    "account.blob.core.windows.net.evil.example",
    "account..blob.core.windows.net",
    ".account.blob.core.windows.net",
    "user@account.blob.core.windows.net",
    "account.blob.core.windows.net:443",
])
@pytest.mark.parametrize("scheme", ["az", "https"])
def test_endpoint_validation_is_deferred_to_native(client_module, host, scheme):
    path = f"{scheme}://{host}/container/blob"
    assert client_module.normalize_azure_path(path) == f"az://{host}/container/blob"
    assert client_module._dacs_get_intercept_file_path("/mnt/cache", path) == (
        f"/mnt/cache/{host}/container/blob"
    )
    client_module.DACSCacheDataLoader.assert_not_called()


@pytest.mark.parametrize("scheme", ["az", "https"])
def test_normalization_is_independent_of_native_configuration(client_module, monkeypatch, scheme):
    path = f"{scheme}://account.blob.example.test/container/blob"
    expected = "az://account.blob.example.test/container/blob"
    monkeypatch.delenv("AZURE_BLOB_ENDPOINT_SUFFIX", raising=False)
    assert client_module.normalize_azure_path(path) == expected
    monkeypatch.setenv("AZURE_BLOB_ENDPOINT_SUFFIX", ".different.example")
    assert client_module.normalize_azure_path(path) == expected
    assert client_module.os.environ["AZURE_BLOB_ENDPOINT_SUFFIX"] == ".different.example"


def test_intercept_path_preserves_embedded_scheme(client_module):
    assert client_module._dacs_get_intercept_file_path(
        "/mnt/cache", "az://account.blob.core.windows.net/container/az://blob"
    ) == "/mnt/cache/account.blob.core.windows.net/container/az://blob"
