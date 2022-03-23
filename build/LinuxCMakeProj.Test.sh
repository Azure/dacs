#!/bin/bash
PATH_TO_SOURCE="$(cd "$(dirname "$0")/.." && pwd )"
. "${PATH_TO_SOURCE}/.pipelines/nativeunittest.sh" $CMAKE_BUILD_CONFIGURATION