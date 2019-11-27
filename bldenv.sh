#!/bin/bash

echo Syncing build environment ...
dotnet msbuild dirs.proj -t:Restore -noConsoleLogger