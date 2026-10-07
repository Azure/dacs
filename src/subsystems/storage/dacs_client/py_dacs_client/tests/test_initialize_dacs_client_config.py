#!/usr/bin/env python3
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

"""Tests for _initialize_dacs_client_config() SAS token handling."""

import os
import stat
from concurrent.futures import ThreadPoolExecutor

import py_dacs_client
import pytest
from py_dacs_client import dacs_utils

# To run the test: 
# cd <workspace>/DACS/src/subsystems/storage/dacs_client
# python3 -m pytest py_dacs_client/tests/test_initialize_dacs_client_config.py -v --noconftest

@pytest.fixture(autouse=True)
def isolated_config_environment(monkeypatch):
    # setenv first so teardown restores the original state even if the code under test sets it.
    monkeypatch.setenv("STORAGE_INTERCEPT_CONFIG_FILEPATH", "")
    monkeypatch.delenv("STORAGE_INTERCEPT_CONFIG_FILEPATH")
    monkeypatch.setattr(dacs_utils, "_generated_config_path", None)


def test_generated_config_is_rewritten_on_repeated_initialization(tmp_path):
    dacs_utils._initialize_dacs_client_config(intercept_path="/mnt/first", config_dir=str(tmp_path))
    config_path = os.environ["STORAGE_INTERCEPT_CONFIG_FILEPATH"]

    dacs_utils._initialize_dacs_client_config(intercept_path="/mnt/second", config_dir=str(tmp_path))

    assert os.environ["STORAGE_INTERCEPT_CONFIG_FILEPATH"] == config_path
    with open(config_path) as config_file:
        contents = config_file.read()
    assert "storagePath /mnt/second\n" in contents
    assert "/mnt/first" not in contents


def test_native_auto_generated_config_is_rewritten(monkeypatch, tmp_path):
    # BlobSiWrapper's dlopen constructor writes this same per-PID path and exports it.
    native_config = tmp_path / f"storageIntercept.{os.getpid()}.config"
    native_config.write_text("storagePath /mnt/native-default\n")
    monkeypatch.setenv("STORAGE_INTERCEPT_CONFIG_FILEPATH", str(native_config))

    dacs_utils._initialize_dacs_client_config(intercept_path="/mnt/dacs_cache", config_dir=str(tmp_path))

    assert os.environ["STORAGE_INTERCEPT_CONFIG_FILEPATH"] == str(native_config)
    contents = native_config.read_text()
    assert "storagePath /mnt/dacs_cache\n" in contents
    assert "/mnt/native-default" not in contents


def test_supplied_config_must_be_a_regular_file(monkeypatch, tmp_path):
    config_dir = tmp_path / "a-directory"
    config_dir.mkdir()
    monkeypatch.setenv("STORAGE_INTERCEPT_CONFIG_FILEPATH", str(config_dir))

    with pytest.raises(FileNotFoundError):
        dacs_utils._initialize_dacs_client_config(config_dir=str(tmp_path / "generated"))

    assert os.environ["STORAGE_INTERCEPT_CONFIG_FILEPATH"] == str(config_dir)
    assert not (tmp_path / "generated").exists()


def test_supplied_config_is_preserved_on_repeated_and_parallel_initialization(monkeypatch, tmp_path):
    config_path = tmp_path / "si_blob.config"
    contents = (
        "multiMountEnabled true\nmountName0 lance\nmountName1 blobfile\n"
        "lancestoragePath /mnt/dacs\nblobfilestoragePath /mnt/dacs_cache\n"
    )
    config_path.write_text(contents)
    original_stat = config_path.stat()
    monkeypatch.setenv("STORAGE_INTERCEPT_CONFIG_FILEPATH", str(config_path))
    generated_dir = tmp_path / "must-not-be-created"

    def initialize(intercept_path):
        dacs_utils._initialize_dacs_client_config(
            intercept_path=intercept_path,
            write_mode=py_dacs_client.WriteMode.WRITE_SYNC,
            config_dir=str(generated_dir),
        )
        assert os.environ["STORAGE_INTERCEPT_CONFIG_FILEPATH"] == str(config_path)

    initialize("/mnt/dacs_cache")
    initialize("/mnt/dacs_cache")
    with ThreadPoolExecutor(max_workers=8) as executor:
        list(executor.map(initialize, ["/mnt/dacs", "/mnt/dacs_cache"] * 16))

    assert config_path.read_text() == contents
    assert config_path.stat().st_mtime_ns == original_stat.st_mtime_ns
    assert not generated_dir.exists()


def test_skipping_config_creation_is_logged(monkeypatch, tmp_path, caplog):
    config_path = tmp_path / "si_blob.config"
    config_path.write_text("multiMountEnabled true\n")
    monkeypatch.setenv("STORAGE_INTERCEPT_CONFIG_FILEPATH", str(config_path))
    # The client logger does not propagate, so attach caplog's handler directly.
    monkeypatch.setattr(dacs_utils.logger, "handlers", [caplog.handler])

    dacs_utils._initialize_dacs_client_config(config_dir=str(tmp_path / "generated"))

    assert f"Using existing storage-intercept config {config_path}" in caplog.text
    assert "skipping config creation" in caplog.text


def test_missing_supplied_config_is_not_replaced(monkeypatch, tmp_path):
    config_path = tmp_path / "missing.config"
    monkeypatch.setenv("STORAGE_INTERCEPT_CONFIG_FILEPATH", str(config_path))

    with pytest.raises(FileNotFoundError):
        dacs_utils._initialize_dacs_client_config(config_dir=str(tmp_path / "generated"))

    assert os.environ["STORAGE_INTERCEPT_CONFIG_FILEPATH"] == str(config_path)
    assert list(tmp_path.iterdir()) == []


class TestInitializeDACSClientConfigSASToken:
    """Verify that SAS tokens with trailing \\r are stripped before being written to the config file."""

    def test_sas_token_trailing_cr_is_stripped(self, monkeypatch, tmp_path):
        dummy_sas = "sv=2021-06-08&ss=b&srt=co&sp=r&sig=zrymHZV2jATs%3D"
        sas_with_cr = dummy_sas + "\r"

        monkeypatch.setenv("USE_SAS_TOKEN", "true")
        monkeypatch.setenv("STORAGE_INTCPT_SAS_TOKEN", sas_with_cr)
        # Ensure no other auth paths are taken
        monkeypatch.delenv("SI_azBlobAccessToken", raising=False)
        monkeypatch.delenv("AZURE_CLIENT_ID", raising=False)
        monkeypatch.delenv("MSI_ENDPOINT", raising=False)
        monkeypatch.delenv("MSI_SECRET", raising=False)
        # Disable cache to keep the test focused
        monkeypatch.delenv("CACHE_DISCOVERY_URL", raising=False)
        monkeypatch.delenv("CACHE_SERVER_LIST", raising=False)
        monkeypatch.delenv("CACHE_SERVER_PORT", raising=False)

        dacs_utils._initialize_dacs_client_config(
            intercept_path="/mnt/data",
            config_dir=str(tmp_path),
        )

        actual_config_path = os.environ["STORAGE_INTERCEPT_CONFIG_FILEPATH"]
        assert os.path.exists(actual_config_path)

        with open(actual_config_path) as f:
            config_contents = f.read()

        # The SAS token line should contain the stripped value with no \r
        assert f"azBlobSASToken {dummy_sas}\n" in config_contents
        assert "\r" not in config_contents

    def test_config_is_owner_only_before_token_write(self, monkeypatch, tmp_path):
        monkeypatch.setenv("USE_SAS_TOKEN", "true")
        monkeypatch.setenv("STORAGE_INTCPT_SAS_TOKEN", "secret-token")

        dacs_utils._initialize_dacs_client_config(
            intercept_path="/mnt/data",
            config_dir=str(tmp_path),
        )

        config_path = os.environ["STORAGE_INTERCEPT_CONFIG_FILEPATH"]
        assert stat.S_IMODE(os.stat(config_path).st_mode) == 0o600
        with open(config_path) as config_file:
            assert "azBlobSASToken secret-token\n" in config_file.read()

    def test_existing_file_permissions_are_preserved(self, monkeypatch, tmp_path):
        monkeypatch.setenv("USE_SAS_TOKEN", "true")
        monkeypatch.setenv("STORAGE_INTCPT_SAS_TOKEN", "secret-token")

        config_path = tmp_path / f"storageIntercept.{os.getpid()}.config"
        config_path.write_text("existing contents\n")
        config_path.chmod(0o644)

        dacs_utils._initialize_dacs_client_config(
            intercept_path="/mnt/data",
            config_dir=str(tmp_path),
        )

        assert "azBlobSASToken secret-token\n" in config_path.read_text()
        assert stat.S_IMODE(config_path.stat().st_mode) == 0o644

    def test_existing_symlink_is_rejected(self, monkeypatch, tmp_path):
        monkeypatch.setenv("USE_SAS_TOKEN", "true")
        monkeypatch.setenv("STORAGE_INTCPT_SAS_TOKEN", "secret-token")

        config_path = tmp_path / f"storageIntercept.{os.getpid()}.config"
        target_path = tmp_path / "target"
        target_path.write_text("target must remain unchanged\n")
        config_path.symlink_to(target_path)

        with pytest.raises(OSError):
            dacs_utils._initialize_dacs_client_config(
                intercept_path="/mnt/data",
                config_dir=str(tmp_path),
            )

        assert target_path.read_text() == "target must remain unchanged\n"

    def test_shared_write_mode_configures_core_client(self, monkeypatch, tmp_path):
        monkeypatch.delenv("USE_SAS_TOKEN", raising=False)
        monkeypatch.delenv("STORAGE_INTCPT_SAS_TOKEN", raising=False)
        monkeypatch.delenv("SI_azBlobAccessToken", raising=False)
        monkeypatch.delenv("AZURE_CLIENT_ID", raising=False)
        monkeypatch.delenv("MSI_ENDPOINT", raising=False)
        monkeypatch.delenv("MSI_SECRET", raising=False)
        monkeypatch.delenv("CACHE_DISCOVERY_URL", raising=False)
        monkeypatch.delenv("CACHE_SERVER_LIST", raising=False)
        monkeypatch.delenv("CACHE_SERVER_PORT", raising=False)

        dacs_utils._initialize_dacs_client_config(
            intercept_path="/mnt/data",
            write_mode=py_dacs_client.WriteMode.WRITE_CACHE_ONLY,
            config_dir=str(tmp_path),
        )

        actual_config_path = os.environ["STORAGE_INTERCEPT_CONFIG_FILEPATH"]
        with open(actual_config_path) as f:
            config_contents = f.read()

        assert "enableWrite true\n" in config_contents
        assert "enableWriteRemote false\n" in config_contents
        assert "enableWriteRemoteAsync false\n" in config_contents

    def test_sas_token_without_trailing_cr(self, monkeypatch, tmp_path):
        dummy_sas = "sv=2021-06-08&ss=b&srt=co&sp=r&sig=zrymHZV2jATs%3D"

        monkeypatch.setenv("USE_SAS_TOKEN", "true")
        monkeypatch.setenv("STORAGE_INTCPT_SAS_TOKEN", dummy_sas)
        monkeypatch.delenv("SI_azBlobAccessToken", raising=False)
        monkeypatch.delenv("AZURE_CLIENT_ID", raising=False)
        monkeypatch.delenv("MSI_ENDPOINT", raising=False)
        monkeypatch.delenv("MSI_SECRET", raising=False)
        monkeypatch.delenv("CACHE_DISCOVERY_URL", raising=False)
        monkeypatch.delenv("CACHE_SERVER_LIST", raising=False)
        monkeypatch.delenv("CACHE_SERVER_PORT", raising=False)

        dacs_utils._initialize_dacs_client_config(
            intercept_path="/mnt/data",
            config_dir=str(tmp_path),
        )

        actual_config_path = os.environ["STORAGE_INTERCEPT_CONFIG_FILEPATH"]
        with open(actual_config_path) as f:
            config_contents = f.read()

        assert f"azBlobSASToken {dummy_sas}\n" in config_contents

    def test_sas_token_with_surrounding_whitespace(self, monkeypatch, tmp_path):
        dummy_sas = "sv=2021-06-08&ss=b&srt=co&sp=r&sig=zrymHZV2jATs%3D"
        sas_with_whitespace = "  " + dummy_sas + " \r\n"

        monkeypatch.setenv("USE_SAS_TOKEN", "true")
        monkeypatch.setenv("STORAGE_INTCPT_SAS_TOKEN", sas_with_whitespace)
        monkeypatch.delenv("SI_azBlobAccessToken", raising=False)
        monkeypatch.delenv("AZURE_CLIENT_ID", raising=False)
        monkeypatch.delenv("MSI_ENDPOINT", raising=False)
        monkeypatch.delenv("MSI_SECRET", raising=False)
        monkeypatch.delenv("CACHE_DISCOVERY_URL", raising=False)
        monkeypatch.delenv("CACHE_SERVER_LIST", raising=False)
        monkeypatch.delenv("CACHE_SERVER_PORT", raising=False)

        dacs_utils._initialize_dacs_client_config(
            intercept_path="/mnt/data",
            config_dir=str(tmp_path),
        )

        actual_config_path = os.environ["STORAGE_INTERCEPT_CONFIG_FILEPATH"]
        with open(actual_config_path) as f:
            config_contents = f.read()

        assert f"azBlobSASToken {dummy_sas}\n" in config_contents
        assert "\r" not in config_contents
