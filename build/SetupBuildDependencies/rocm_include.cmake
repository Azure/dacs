# Globally available cuda (rocm) variables
if(${CUDA_VERSION} MATCHES "rocm_5_2_3")
    message("Set Variables for rocm 5.2.3 ${CMAKE_SOURCE_DIR}")
    set(DEVICE_PROXY_NCCL_VERSION "2.12.12")
    set(DEVICE_PROXY_CUDA_VERSION "0.0.0") # Until codegen for AMD is ready, use noop cpi stubs.
    set(ENV{CUDA_ROOT} "/opt/rocm-5.2.3/")
    set(ENV{NCCL_ROOT} "/opt/rocm-5.2.3/rccl")

    link_directories("$ENV{CUDA_ROOT}/targets/x86_64-linux/lib/stubs")
endif()

# Required setup to make ROCM toolchain target AMD ( as it supports both NVIDIA/AMD )
add_definitions(-D__HIP_PLATFORM_AMD__)
add_definitions(-DAMD_BUILD)
set(HIP_PATH "$ENV{CUDA_ROOT}/hip" CACHE PATH "Path to which HIP has been installed")

list(APPEND CMAKE_PREFIX_PATH "${HIP_PATH}/cmake")
list(APPEND CMAKE_MODULE_PATH "${HIP_PATH}/cmake")
set(HIP_LIB_TYPE "shared")
find_package(HIP REQUIRED)

if(HIP_FOUND)
    message(STATUS "Found HIP: " ${HIP_VERSION})
else()
    message(FATAL_ERROR "Could not find HIP. Ensure that HIP is either installed in /opt/rocm-*/hip or the variable HIP_PATH is set to point to the right location.")
endif()

# List of GPUs to target must be manually proivded, otherwise defaults to GPUs available at build time.
set(CMAKE_HIP_ARCHITECTURES gfx908)

include_directories("$ENV{CUDA_ROOT}/include/hip")
include_directories("$ENV{CUDA_ROOT}/include/rocblas")
include_directories("$ENV{CUDA_ROOT}/include/rccl")
include_directories("$ENV{CUDA_ROOT}/include/hsa")
include_directories("$ENV{CUDA_ROOT}/include/roctracer")
include_directories("$ENV{CUDA_ROOT}/include")

link_directories("$ENV{CUDA_ROOT}/lib")
link_directories("$ENV{NCCL_ROOT}/lib")

# Aliases for linked libraries
set(CUDART_LIB "-lroctx64")
set(CUDA_LIB "-lamdhip64")
set(CUBLAS_LIB "-lrocblas")
set(NCCL_LIB "-lrccl")
set(CUDNN_LIB "-lMIOpen")
set(NVRTC_LIB "-lhiprtc")
set(GPU_PROFILER_LIB "-lroctracer64")