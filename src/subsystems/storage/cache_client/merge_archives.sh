#!/bin/bash
# Copyright (c) Microsoft Corporation.
# Licensed under the Apache License, Version 2.0.

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
