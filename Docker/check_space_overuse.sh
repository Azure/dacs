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

# Script to run before Cache Server is started
# Command => check_space_overuse.sh <CACHESERVER_FOLDER_PATH> <CACHESERVER_CACHE_SIZE_BYTES>

CACHESERVER_FOLDER_PATH=$1
CACHESERVER_CACHE_SIZE_BYTES=$2

if [ -z "$CACHESERVER_FOLDER_PATH" ] || [ -z "$CACHESERVER_CACHE_SIZE_BYTES" ]; then
  echo "Usage: $0 <CACHESERVER_FOLDER_PATH> <CACHESERVER_CACHE_SIZE_BYTES>"
  exit 1
fi

if [ ! -d "$CACHESERVER_FOLDER_PATH" ]; then
  echo "Cache directory $CACHESERVER_FOLDER_PATH does not exist."
  exit 0
fi

cache_size=$(du -sb "$CACHESERVER_FOLDER_PATH" | awk '{print $1}')
if [ "$cache_size" -gt "$CACHESERVER_CACHE_SIZE_BYTES" ]; then
  echo "Cache directory size $cache_size exceeds the limit of $CACHESERVER_CACHE_SIZE_BYTES bytes."
  echo "Clearing cache directory $CACHESERVER_FOLDER_PATH."
  find "$CACHESERVER_FOLDER_PATH" -mindepth 1 -delete
  if [ $? -ne 0 ]; then
    echo "Failed to clear cache directory."
    exit 1
  fi
  echo "Cache directory cleared successfully."
else
  echo "Cache directory size $cache_size is within the limit of $CACHESERVER_CACHE_SIZE_BYTES bytes."
fi