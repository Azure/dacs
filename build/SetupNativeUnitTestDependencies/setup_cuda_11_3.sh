#!/bin/bash -e

echo "Setting dependencies for CUDA 11.3 tests"
export DACS_CUOBJDUMP_BIN_LOCATION="/opt/conda-cuda-113/bin/cuobjdump"
export SM_ARCHITECTURE="sm_60"
