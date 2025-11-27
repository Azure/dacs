# Toolchain file for multi-architecture builds

# Detect architecture from environment variable
if(DEFINED ENV{BUILD_ARCHITECTURE})
    set(BUILD_ARCHITECTURE $ENV{BUILD_ARCHITECTURE})
else()
    execute_process(
        COMMAND uname -m
        OUTPUT_VARIABLE ARCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(ARCH STREQUAL "aarch64")
        set(BUILD_ARCHITECTURE "arm64")
    elseif(ARCH STREQUAL "x86_64")
        set(BUILD_ARCHITECTURE "x86_64")
    else()
        message(FATAL_ERROR "Unsupported architecture: ${ARCH}")
    endif()
endif()

# Set architecture-specific flags and settings
if(BUILD_ARCHITECTURE STREQUAL "arm64")
    set(CMAKE_SYSTEM_NAME Linux)
    set(CMAKE_SYSTEM_PROCESSOR aarch64)
    
    # Set specific compile flags for ARM64
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv8-a")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv8-a")
    
    # Library directories specific to ARM64
    set(CMAKE_LIBRARY_PATH /usr/lib/aarch64-linux-gnu)
    set(CMAKE_FIND_ROOT_PATH /usr/lib/aarch64-linux-gnu)
elseif(BUILD_ARCHITECTURE STREQUAL "x86_64")
    set(CMAKE_SYSTEM_NAME Linux)
    set(CMAKE_SYSTEM_PROCESSOR x86_64)
    
    # Set specific compile flags for x86_64
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=x86-64")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=x86-64")
    
    # Library directories specific to x86_64
    set(CMAKE_LIBRARY_PATH /usr/lib/x86_64-linux-gnu)
    set(CMAKE_FIND_ROOT_PATH /usr/lib/x86_64-linux-gnu)
endif()

# Common settings
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

# Enable position independent code
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
