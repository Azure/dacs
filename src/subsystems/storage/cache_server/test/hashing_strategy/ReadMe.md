# Hashing Strategy Benchmark Framework

A comprehensive C++ framework for benchmarking and comparing different hashing strategies.

## Overview

This project implements and benchmarks three popular hashing strategies:
- **Static Hashing**
- **Consistent Hashing**
- **Rendezvous Hashing**

Each strategy can be tested with 5 different hash functions to analyze performance and distribution characteristics.

## Build Instructions

### Prerequisites
- Docker
- The DACS native-build image, which contains the pinned MurmurHash3, xxHash,
  and OpenSSL dependencies

### Build with CMake (Recommended)
```bash
# Run from the repository root
./bldenv.sh /bin/bash '
  cmake -S src/subsystems/storage/cache_server/test/hashing_strategy \
    -B build/hashing_strategy -G Ninja &&
  cmake --build build/hashing_strategy &&
  ctest --test-dir build/hashing_strategy --output-on-failure
'
```

## Test Instructions

### Basic Usage
```bash
# Run default test (FNV-1a with all strategies)
./HashingStrategyTest

# Show help
./HashingStrategyTest --help
```

### Available Commands

#### Full Matrix Test
```bash
# Test all hash functions × all strategies (15 combinations)
./HashingStrategyTest matrix
```

#### Quick Performance Summary
```bash
# Compact table showing all combinations
./HashingStrategyTest quick
```

#### Test Specific Hash Function
```bash
# Test all strategies with one hash function
./HashingStrategyTest hash sha256
```

#### Test Single Combination
```bash
# Test one strategy + hash combination
./HashingStrategyTest single consistent murmur
```