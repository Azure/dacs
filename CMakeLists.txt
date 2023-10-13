# Project name, build system and c++ standards used
cmake_minimum_required(VERSION 3.19)
project(DACS LANGUAGES CXX C)

set(CMAKE_BINARY_DIR $ENV{CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY $ENV{CMAKE_LIBRARY_OUTPUT_DIRECTORY})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY $ENV{CMAKE_RUNTIME_OUTPUT_DIRECTORY})
set(CMAKE_REMOTE_PUBLISH_DIRECTORY $ENV{CMAKE_REMOTE_PUBLISH_DIRECTORY})
set(CTEST_REMOTE_LIB_DIRECTORY $ENV{CTEST_REMOTE_LIB_DIRECTORY})
set(CUDA_USE_STATIC_CUDA_RUNTIME OFF CACHE INTERNAL "")

# Default standard in the project is C99 & C++17
set(CMAKE_CXX_STANDARD 17)
set(CXX_STANDARD_REQUIRED $ENV{CXX_STANDARD_REQUIRED})
set(CMAKE_C_STANDARD $ENV{CMAKE_C_STANDARD})
set(C_STANDARD_REQUIRED $ENV{C_STANDARD_REQUIRED})

# Directory containing external project import files
set(CMAKE_DEP_INSTALL_DIR $ENV{CMAKE_DEP_INSTALL_DIR})
set(CMAKE_DEP_CMAKE_DIR ${CMAKE_DEP_INSTALL_DIR}/cmake)
set(CMAKE_EXTERNAL_PROJECT ${CMAKE_DEP_CMAKE_DIR}/external)
set(CMAKE_MODULE_PATH ${CMAKE_DEP_CMAKE_DIR}/external)

add_definitions(-DBUILD_GIT_COMMIT_HASH="${GIT_COMMIT_HASH}")

# Treat warnings as errors
add_compile_options(-Werror)

# Compile time symbol checking like check_cxx_symbol_exists(std::fopen "cstdio" HAVE_STD_FOPEN)
include(CheckCXXSymbolExists)

# Add DL libs
link_libraries(${CMAKE_DL_LIBS})

# Add boost headers
find_package(Boost REQUIRED)
include_directories(${Boost_INCLUDE_DIR})

# Enable referencing git projects dependencies
include(ExternalProject)

# Include all pre-built dependency projects
add_subdirectory(${CMAKE_DEP_INSTALL_DIR}/cmake ${CMAKE_DEP_INSTALL_DIR} EXCLUDE_FROM_ALL)

# The GTest git hub project for testing and testing related vars
include(${CMAKE_EXTERNAL_PROJECT}/gtest_include.cmake)

# The spdlog git hub project for logging and related vars
include(${CMAKE_EXTERNAL_PROJECT}/spdlog_include.cmake)

# serialization support through json. It supports json, bson, cbor etc.
include(${CMAKE_EXTERNAL_PROJECT}/json_include.cmake)

#Fast Hashing with xxHash - required for criu deduping
include(${CMAKE_EXTERNAL_PROJECT}/xxHash_include.cmake)

#prometheus support through https://github.com/jupp0r/prometheus-cpp/releases 
include(${CMAKE_EXTERNAL_PROJECT}/prometheus_cpp_include.cmake)

if(AMD_BUILD)
    # Include ROCm HIP, RCCL, etc.
    include(build/SetupBuildDependencies/rocm_include.cmake)
    set(GPU_TYPE "amd")

    # hipError_t is marked as [[nodiscard]] which gives a warning when
    # its not used. Disable that warning.
    add_compile_options(-Wno-unused-result)
    message(STATUS "AMD Build is enabled")
else()
    # Include CUDA, NCCL and CuDNN libraries
    include(build/SetupBuildDependencies/cuda_include.cmake)
    set(GPU_TYPE "nvidia")
    message(STATUS "AMD Build is disabled")
endif()

# Utilities for simplifying AMD/NVIDIA specific builds
include(build/SetupBuildDependencies/build_utils.cmake)

# Azure storage lite lib - to communicate with blob services
include(${CMAKE_EXTERNAL_PROJECT}/storagelite_include.cmake)

# Google protobuf static lib
include(${CMAKE_EXTERNAL_PROJECT}/protobufstatic_include.cmake)

# ctest
include(CTest)

# Globaly available headers from dacs project
include_directories(src/subsystems/common/native_lib/diagnostics/)
include_directories(src/subsystems/common/native_lib/utils/)
include_directories(src/subsystems/reliability/callstack_utils/)

# Enable testing at the root level
enable_testing()

# Need to configure Doxygen
# Need to configure Astyle
# Need to configure cppcheck

# The list of sub projects
add_subdirectory(src/subsystems/common/native_lib/diagnostics)
add_subdirectory(src/subsystems/common/native_lib/diagnostics.tests)
add_subdirectory(src/subsystems/reliability/common/native/)
add_subdirectory(src/subsystems/reliability/callstack_utils/)

message("CUDA_VERSION: ${CUDA_VERSION}")

if(NOT CUDA_VERSION OR NOT ("${CUDA_VERSION}" MATCHES "non_cuda"))
    message("add CUDA dependant projects")
    add_subdirectory(src/subsystems/reliability/device_proxy/)
endif()

if(NOT CUDA_VERSION OR "${CUDA_VERSION}" MATCHES "non_cuda")
    message("add non CUDA dependant projects")
    add_subdirectory(src/subsystems/reliability/criu_deduper/)
    add_subdirectory(src/subsystems/reliability/lfs_ckpt/)
    add_subdirectory(src/subsystems/reliability/sys_interceptors/)
    add_subdirectory(src/subsystems/reliability/amd_nccl_intercept/)
    add_subdirectory(src/subsystems/storage/common/)
    add_subdirectory(src/subsystems/storage/common/proto/)
    add_subdirectory(src/subsystems/storage/common/libc_wrapper/)
    add_subdirectory(src/subsystems/storage/cache_client/connection/)
    add_subdirectory(src/subsystems/storage/cache_client/encryption_utils/)
    add_subdirectory(src/subsystems/storage/storage_intercept/)
    add_subdirectory(src/subsystems/storage/cache_server/)
    add_subdirectory(src/subsystems/storage/cache_client/)
    add_subdirectory(src/subsystems/storage/cache_client/liveness_probe/)
    add_subdirectory(src/subsystems/storage/utils/)
    add_subdirectory(src/subsystems/storage/stress_tests/)
    add_subdirectory(src/subsystems/reliability/ab_test/)
    add_subdirectory(src/subsystems/reliability/ab_test.tests/)

    set(CMAKE_DYNAMIC_PATCHES_PATH "${CMAKE_SOURCE_DIR}/src/subsystems/reliability/dynamic_patches/")
    add_custom_target(copy_dynamic_patches ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_DYNAMIC_PATCHES_PATH} ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/dynamic_patches)
endif()
