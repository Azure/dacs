#!/bin/bash
set -x
echo "Set up build dependencies for cuda 10.1"
rm -f /etc/alternatives/libcudnn
ln -s /usr/include/x86_64-linux-gnu/cudnn_v7.h /etc/alternatives/libcudnn
rm -f /usr/local/cuda
ln -s /usr/local/cuda-10.1/ /usr/local/cuda