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

# Usage: ./merge_archives.sh <output_archive> <input_archive1> <input_archive2>
# Description: Merges two archives into one
set -e

echo "Merging archives $2 and $3 into $1"

ar -M <<EOM
CREATE $1
ADDLIB $2
ADDLIB $3
SAVE
END
EOM

echo "Done"
