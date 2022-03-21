#!/bin/bash -e

echo "Setting dependencies for CUDA 11.1 tests"
export DACS_CUOBJDUMP_BIN_LOCATION="/opt/conda-cuda-111/bin/cuobjdump"
export SM_ARCHITECTURE="sm_60"