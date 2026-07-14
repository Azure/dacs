# The storage_utils project with relevent dependency imports
project(storage_utils)
find_package(Threads REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG ON)

# Files to build as the libstorage_utils
add_library(storage_utils 
    ThreadPool.cpp)

target_include_directories(storage_utils INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# Wiring things up for compile and the runtime
target_link_libraries(storage_utils PRIVATE -L${DEP_INSTALL_DIR}/lib)
target_link_libraries(storage_utils PRIVATE Threads::Threads)
