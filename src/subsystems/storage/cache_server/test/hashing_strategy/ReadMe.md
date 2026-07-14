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
- C++17 compatible compiler (GCC 7+, Clang 5+)
- CMake 3.10 or higher
- OpenSSL development libraries

### Install Dependencies (Ubuntu/Debian)
```bash
sudo apt update
sudo apt install build-essential cmake libssl-dev
```

### Build with CMake (Recommended)
```bash
# Navigate to project directory
cd hashing_strategy

# Create build directory
mkdir build && cd build

# Configure and build
cmake ..
make -j$(nproc)
```

### Build with g++ (Alternative)
```bash
g++ -std=c++17 -O2 -Wall -Wextra -Wno-implicit-fallthrough \
    -I hash_functions -I strategies -I test \
    test/HashingStrategyTest.cpp \
    test/HashingStrategyBenchmark.cpp \
    strategies/StaticHashingStrategy.cpp \
    strategies/ConsistentHashingStrategy.cpp \
    strategies/RendezvousHashingStrategy.cpp \
    hash_functions/HashFunctions.cpp \
    hash_functions/MurmurHash3.cpp \
    hash_functions/xxhash.c \
    -lssl -lcrypto -o HashingStrategyTest
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