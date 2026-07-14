# Fetch content
#
include(FetchContent)

# Disable Azure SDK features we don't need
set(AZURE_SDK_DISABLE_AUTO_VCPKG ON CACHE BOOL "" FORCE)
set(DISABLE_AZURE_CORE_OPENTELEMETRY ON CACHE BOOL "" FORCE)
set(BUILD_WINDOWS_UWP ON CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_PERFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
set(WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(azuresdkforcpp
    # Set the SDK URL path and release tag
    GIT_REPOSITORY      https://github.com/Azure/azure-sdk-for-cpp.git
    GIT_TAG             azure-storage-blobs_12.13.0
)
FetchContent_GetProperties(azuresdkforcpp)
if(NOT azuresdkforcpp_POPULATED)
    FetchContent_Populate(azuresdkforcpp)
    add_subdirectory(${azuresdkforcpp_SOURCE_DIR} ${azuresdkforcpp_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()
