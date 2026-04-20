#!/usr/bin/env python3
"""Tests for _initialize_dacs_client_config() SAS token handling."""

import os
import pytest
from py_dacs_client import dacs_utils

# To run the test: 
# cd <workspace>/DACS/src/subsystems/storage/dacs_client
# python3 -m pytest py_dacs_client/tests/test_initialize_dacs_client_config.py -v --noconftest

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

        # Redirect the config to a temp directory so we don't pollute /tmp
        unique_id = tmp_path.name
        monkeypatch.setattr(os, "getpid", lambda: unique_id)

        dacs_utils._initialize_dacs_client_config(intercept_path="/mnt/data")

        actual_config_path = os.environ["STORAGE_INTERCEPT_CONFIG_FILEPATH"]
        assert os.path.exists(actual_config_path)

        with open(actual_config_path) as f:
            config_contents = f.read()

        # The SAS token line should contain the stripped value with no \r
        assert f"azBlobSASToken {dummy_sas}\n" in config_contents
        assert "\r" not in config_contents

        # Clean up the config file after the test
        if os.path.exists(actual_config_path):
            os.remove(actual_config_path)

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

        monkeypatch.setattr(os, "getpid", lambda: "test")

        dacs_utils._initialize_dacs_client_config(intercept_path="/mnt/data")

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

        monkeypatch.setattr(os, "getpid", lambda: "test")

        dacs_utils._initialize_dacs_client_config(intercept_path="/mnt/data")

        actual_config_path = os.environ["STORAGE_INTERCEPT_CONFIG_FILEPATH"]
        with open(actual_config_path) as f:
            config_contents = f.read()

        assert f"azBlobSASToken {dummy_sas}\n" in config_contents
        assert "\r" not in config_contents
