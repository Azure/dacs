project(storage_intercept_common LANGUAGES CXX C)
set(CMAKE_CXX_FLAGS " ${CMAKE_CXX_FLAGS_INIT} -fPIC")

add_library(StorageInterceptLibcDirect StorageLibcDirectWrapper.cpp)
add_library(StorageInterceptLibcDlsym StorageLibcDlsymWrapper.cpp)

set(IncludeDirs ${CMAKE_SOURCE_DIR}/src/subsystems/storage/storage_intercept/common/)
set(Links StorageCommon diagnostics curl jsoncpp dl uuid crypto ibverbs)
set(Dependencies diagnostics)

target_include_directories(StorageInterceptLibcDirect PRIVATE ${IncludeDirs})
target_include_directories(StorageInterceptLibcDlsym PRIVATE ${IncludeDirs})

add_dependencies(StorageInterceptLibcDirect ${Dependencies})
add_dependencies(StorageInterceptLibcDlsym ${Dependencies})

target_link_libraries(StorageInterceptLibcDirect ${Links})
target_link_libraries(StorageInterceptLibcDlsym ${Links})
