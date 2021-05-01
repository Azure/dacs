#!/bin/bash
set -x
echo "Set up build dependencies for cuda 11.0"
rm -f /etc/alternatives/libcudnn
ln -s /usr/include/x86_64-linux-gnu/cudnn_v8.h /etc/alternatives/libcudnn
rm -f /usr/local/cuda
ln -s /usr/local/cuda-11.0/ /usr/local/cuda