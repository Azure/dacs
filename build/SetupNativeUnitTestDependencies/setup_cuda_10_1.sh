#!/bin/bash -e

echo "Setting dependencies for CUDA 10.1 tests"
export DACS_CUOBJDUMP_BIN_LOCATION="/opt/conda-cuda-101/bin/cuobjdump"
gpu_type=$(nvidia-smi --format=csv,noheader --query-gpu=name | head -1)
echo $gpu_type
if [[ $gpu_type =~ "A100" ]]; then
    export SM_ARCHITECTURE="sm_80"
elif [[ $gpu_type =~ "V100" ]]; then
    export SM_ARCHITECTURE="sm_70"
elif [[ $gpu_type =~ "K80" ]]; then
    export SM_ARCHITECTURE="sm_37"
elif [[ $gpu_type =~ "RTX 4000" ]]; then
    export SM_ARCHITECTURE="sm_75"
elif [[ $gpu_type =~ "P100" ]]; then
    export SM_ARCHITECTURE="sm_60"
fi
