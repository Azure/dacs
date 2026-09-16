#!/bin/bash
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

set -euo pipefail

readonly SUPPLY_CHAIN_CA_URL="https://www.microsoft.com/pkiops/certs/Microsoft%20Supply%20Chain%20RSA%20Root%20CA%202022.crt"
readonly SUPPLY_CHAIN_CA_SHA256="23ffe2b8bdb9a1711515d4cffda04bc7f793d513c76c243f1020507d8669b7db"
readonly ESRP_TSA_URL="https://www.microsoft.com/pki/certs/MicRooCerAut_2010-06-23.crt"
readonly ESRP_TSA_SHA256="df545bf919a2439c36983b54cdfc903dfa4f37d3996d8d84b4c31eec6f3c163e"

if (($# != 1)); then
    echo "Usage: $0 <trust-store-directory>" >&2
    exit 1
fi

TRUST_STORE_ROOT="$1"
SUPPLY_CHAIN_STORE="$TRUST_STORE_ROOT/x509/ca/supplychain"
ESRP_TSA_STORE="$TRUST_STORE_ROOT/x509/tsa/esrp"
TEMP_DIR="$(mktemp -d)"
SUPPLY_CHAIN_DOWNLOAD="$TEMP_DIR/supply-chain-ca.der"
ESRP_TSA_DOWNLOAD="$TEMP_DIR/esrp-tsa.der"

cleanup() {
    rm -f -- "$SUPPLY_CHAIN_DOWNLOAD" "$ESRP_TSA_DOWNLOAD"
    rmdir -- "$TEMP_DIR"
}
trap cleanup EXIT

download_and_verify() {
    local url="$1"
    local expected_sha256="$2"
    local downloaded="$3"
    local destination="$4"

    curl \
        --fail \
        --location \
        --proto '=https' \
        --retry 3 \
        --show-error \
        --silent \
        "$url" \
        --output "$downloaded"

    echo "$expected_sha256  $downloaded" | sha256sum --check --status
    openssl x509 -in "$downloaded" -inform DER -out "$destination" -outform PEM
}

mkdir -p "$SUPPLY_CHAIN_STORE" "$ESRP_TSA_STORE"

download_and_verify \
    "$SUPPLY_CHAIN_CA_URL" \
    "$SUPPLY_CHAIN_CA_SHA256" \
    "$SUPPLY_CHAIN_DOWNLOAD" \
    "$SUPPLY_CHAIN_STORE/microsoft-supply-chain-rsa-root-ca-2022.crt"

download_and_verify \
    "$ESRP_TSA_URL" \
    "$ESRP_TSA_SHA256" \
    "$ESRP_TSA_DOWNLOAD" \
    "$ESRP_TSA_STORE/microsoft-root-certificate-authority-2010.crt"
