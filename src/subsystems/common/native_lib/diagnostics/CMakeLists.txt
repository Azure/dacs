# The diagnostics project with relevent dependency imports
project(diagnostics)
find_package(Threads REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG ON)

# Files to build as the libdiagnostics
add_library(diagnostics 
    FileLogger.cpp
    StandardLogger.cpp
    LoggingApi.cpp)

set(CMAKE_CXX_FLAGS " ${CMAKE_CXX_FLAGS_INIT} -fPIC")
# Wiring things up for compile and the runtime
target_link_libraries(diagnostics PRIVATE -L${DEP_INSTALL_DIR}/lib)
target_link_libraries(diagnostics PRIVATE Threads::Threads)
