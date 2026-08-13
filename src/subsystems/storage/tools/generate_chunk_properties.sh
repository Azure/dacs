#!/usr/bin/env bash
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

# Fail on unset vars and non-zero exit codes. Enable pipefail when supported.
# Some shells (e.g., dash or when invoked under PowerShell sh emulation) don't support
# 'pipefail' which previously caused: "set: Illegal option -o pipefail".
set -eu
# Try to enable pipefail in shells that support it (bash, ksh, zsh). Ignore errors.
{ set -o pipefail; } 2>/dev/null || true

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
  cat <<EOF
Usage: $(basename "$0") <account> <container> <blob_path> <blob_size> <chunk_size> <ignore_account_name:true|false> <num_servers> [output_file=chunk_properties.json]

This script calls the GetCacheChunkProperties executable for every chunk of a blob and
collects the JSON output into an array in the output file.

Arguments:
  account                Storage account name
  container              Container name
  blob_path              Path to the blob inside the container
  blob_size              Size of the blob in bytes (integer)
  chunk_size             Chunk size in bytes (integer)
  ignore_account_name    true or false — whether to ignore account in cache key
  num_servers            Number of cache servers (required, integer > 0)
  output_file            (optional) File to write JSON array into. Defaults to chunk_properties.json

Example:
  $(basename "$0") myaccount mycontainer path/to/file.txt 10485760 4194304 false 4 chunks.json
EOF
}

if [ "$#" -lt 7 ] || [ "$#" -gt 8 ]; then
  usage
  exit 2
fi

ACCOUNT="$1"
CONTAINER="$2"
BLOB_PATH="$3"
BLOB_SIZE="$4"
CHUNK_SIZE="$5"
IGNORE_ACCOUNT="$6"
NUM_SERVERS="$7"
OUTFILE="${8:-chunk_properties.json}"

validate_numeric_args() {
  re='^[0-9]+$'
  if ! [[ $BLOB_SIZE =~ $re ]]; then
    echo "Error: blob_size must be a non-negative integer" >&2
    exit 2
  fi
  if ! [[ $CHUNK_SIZE =~ $re ]]; then
    echo "Error: chunk_size must be a positive integer" >&2
    exit 2
  fi
  if [ "$CHUNK_SIZE" -eq 0 ]; then
    echo "Error: chunk_size must be greater than 0" >&2
    exit 2
  fi
  if ! [[ $NUM_SERVERS =~ $re ]]; then
    echo "Error: num_servers must be a positive integer" >&2
    exit 2
  fi
  if [ "$NUM_SERVERS" -eq 0 ]; then
    echo "Error: num_servers must be greater than 0" >&2
    exit 2
  fi
}

setup_logging_env() {
  # Configure the logging subsystem used by the tools so that LOG_INFO/LOG_ERROR
  # do not write to stdout/stderr (which we use for JSON output). We set the
  # override env vars so the child process will write logs to files next to the
  # output JSON. This prevents log lines from corrupting the JSON file.
  LOG_OUT_FILE="${SCRIPT_DIR}/${OUTFILE}.log"
  LOG_ERR_FILE="${SCRIPT_DIR}/${OUTFILE}.err"
  export DACS_LOG_OUTPUT_FILE_OVERRIDE="$LOG_OUT_FILE"
  export DACS_LOG_ERROR_FILE_OVERRIDE="$LOG_ERR_FILE"
  # Ensure the files are present/writable
  mkdir -p "$(dirname "$LOG_OUT_FILE")"
  touch "$LOG_OUT_FILE" "$LOG_ERR_FILE" 2>/dev/null || true
}

generate_chunk_properties() {
  # Find the executable: prefer one next to this script, otherwise fallback to PATH
  EXE_PATH="$SCRIPT_DIR/GetCacheChunkProperties"
  if [ ! -x "$EXE_PATH" ]; then
    if command -v GetCacheChunkProperties >/dev/null 2>&1; then
      EXE_PATH="$(command -v GetCacheChunkProperties)"
    else
      echo "Error: GetCacheChunkProperties executable not found next to script or in PATH." >&2
      echo "Put the executable next to this script or install it into your PATH." >&2
      exit 3
    fi
  fi

  # Compute number of chunks (ceil division)
  NUM_CHUNKS=$(( (BLOB_SIZE + CHUNK_SIZE - 1) / CHUNK_SIZE ))

  echo "Generating properties for $NUM_CHUNKS chunk(s) (blob_size=$BLOB_SIZE, chunk_size=$CHUNK_SIZE)"

  # Build JSON array
  rm -f "$OUTFILE"
  echo "[" > "$OUTFILE"
  FIRST=true

  # Directory to capture any bad output or stderr per chunk for debugging
  BAD_DIR="${OUTFILE}.bad"
  rm -rf "$BAD_DIR"
  mkdir -p "$BAD_DIR"
  INVALID_COUNT=0

  for (( i=0; i<NUM_CHUNKS; i++ )); do
    OFFSET=$(( i * CHUNK_SIZE ))
    echo "Processing chunk $((i+1))/$NUM_CHUNKS (offset=$OFFSET)" >&2

    # Call the executable and capture JSON output; save stderr for debugging
    CHUNK_STDERR_FILE="$BAD_DIR/stderr_${i}.log"
    CHUNK_JSON="$("$EXE_PATH" "$ACCOUNT" "$CONTAINER" "$BLOB_PATH" "$OFFSET" "$CHUNK_SIZE" "$IGNORE_ACCOUNT" "$NUM_SERVERS" 2> "$CHUNK_STDERR_FILE")"
    RET=$?
    if [ $RET -ne 0 ]; then
      echo "Warning: GetCacheChunkProperties failed (exit $RET) for offset=$OFFSET (chunk $((i+1))). See $CHUNK_STDERR_FILE" >&2
      INVALID_COUNT=$((INVALID_COUNT+1))
      continue
    fi

    # Trim whitespace/newlines
    CHUNK_JSON="$(echo "$CHUNK_JSON" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')"

    # Validate JSON using jq (if available)
    if command -v jq >/dev/null 2>&1; then
      if ! echo "$CHUNK_JSON" | jq -e '.' >/dev/null 2>&1; then
        echo "Warning: Invalid JSON produced for offset=$OFFSET (chunk $((i+1))). Saving bad output to $BAD_DIR/chunk_${i}.json" >&2
        printf '%s' "$CHUNK_JSON" > "$BAD_DIR/chunk_${i}.json"
        INVALID_COUNT=$((INVALID_COUNT+1))
        continue
      fi
    fi

    if [ "$FIRST" = true ]; then
      echo "$CHUNK_JSON" >> "$OUTFILE"
      FIRST=false
    else
      echo ",$CHUNK_JSON" >> "$OUTFILE"
    fi

  done

  echo "]" >> "$OUTFILE"

  if [ $INVALID_COUNT -gt 0 ]; then
    echo "Completed with $INVALID_COUNT invalid chunk(s). See $BAD_DIR for details." >&2
  fi

  echo "Wrote chunk properties for $NUM_CHUNKS chunk(s) to $OUTFILE"
}

preview_chunk_properties() {
  # Print a short preview
  if command -v jq >/dev/null 2>&1; then
    echo "Preview (first 3 items - selected fields):"
    # Show only the requested fields for the first three items
    jq '.[0:3] | map({Offset, CacheKey, ServerIndex, ServerFilePath})' "$OUTFILE" || true
  else
    echo "If you have 'jq' installed you can preview the output with: jq '.[0:3]' $OUTFILE"
  fi
}

validate_numeric_args

setup_logging_env

generate_chunk_properties

preview_chunk_properties
