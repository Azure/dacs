ExternalProject_Add(
        protobufstatic
        URL https://github.com/protocolbuffers/protobuf/releases/download/v3.0.0/protobuf-cpp-3.0.0.tar.gz
        SOURCE_SUBDIR cmake
        CMAKE_ARGS 
                -DBUILD_SHARED_LIBS=OFF
                -DCMAKE_INSTALL_PREFIX=/opt/protobufstatic-3.0.0
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)
