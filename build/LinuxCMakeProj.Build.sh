#!/bin/bash

if [ -z "$1" ]
then
  echo Missing Required Command Line Parameter: PATH_TO_SOURCE
  exit 1
else
  PATH_TO_SOURCE=$1
fi

# mitigation for container timing issue where current folder may not yet have write permissions
n=0
until [ "$n" -ge 5 ]
do
   touch CMakeCache.txt && break
   n=$((n+1)) 
   sleep 5s
done

cmake -DCMAKE_BUILD_TYPE=$CMAKE_BUILD_CONFIGURATION $PATH_TO_SOURCE
make -j

if [ $? -ne 0 ]
  then
    exit 1
fi