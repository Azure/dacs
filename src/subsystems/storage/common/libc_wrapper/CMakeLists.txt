project(storage_intercept_common LANGUAGES CXX C)
set(CMAKE_CXX_FLAGS " ${CMAKE_CXX_FLAGS_INIT} -fPIC")

add_library(StorageLibcDirectWrapper StorageLibcDirectWrapper.cpp)
add_library(StorageLibcDlsymWrapper StorageLibcDlsymWrapper.cpp)

set(IncludeDirs ${CMAKE_CURRENT_LIST_DIR})
set(Links StorageCommon diagnostics curl jsoncpp dl uuid crypto)
set(Dependencies diagnostics)

target_include_directories(StorageLibcDirectWrapper PUBLIC ${IncludeDirs})
target_include_directories(StorageLibcDlsymWrapper PUBLIC ${IncludeDirs})

add_dependencies(StorageLibcDirectWrapper ${Dependencies})
add_dependencies(StorageLibcDlsymWrapper ${Dependencies})

target_link_libraries(StorageLibcDirectWrapper ${Links})
target_link_libraries(StorageLibcDlsymWrapper ${Links})
