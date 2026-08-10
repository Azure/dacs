ExternalProject_Add(
        protobufstatic
        # Pin the official protobuf C++ 3.21.12 release archive by SHA-256.
        URL "https://github.com/protocolbuffers/protobuf/releases/download/v21.12/protobuf-cpp-3.21.12.tar.gz"
        URL_HASH "SHA256=4eab9b524aa5913c6fffb20b2a8abf5ef7f95a80bc0701f3a6dbb4c607f73460"
        CMAKE_ARGS 
                -DBUILD_SHARED_LIBS=OFF
                -DCMAKE_INSTALL_PREFIX=/opt/protobufstatic-3.0.0
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -Dprotobuf_BUILD_TESTS=OFF
                -DCMAKE_CXX_FLAGS="-Wno-deprecated-declarations"
)
