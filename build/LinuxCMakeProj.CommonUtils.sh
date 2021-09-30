#!/bin/bash
# When calling execute_rsync, first 2 args must be source and destination,
# followed by options
function execute_rsync() {
  local src=$1
  local dst=$2
  mkdir -p $src
  mkdir -p $dst

  echo Executing rsync $*
  rsync $*

  if [ $? -ne 0 ]; then
    echo Error: rsync $* failed.
    exit 1
  fi
}