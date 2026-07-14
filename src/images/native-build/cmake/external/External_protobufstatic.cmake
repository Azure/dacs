ExternalProject_Add(
        protobufstatic
        GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
        GIT_TAG "v3.21.12"
        CMAKE_ARGS 
                -DBUILD_SHARED_LIBS=OFF
                -DCMAKE_INSTALL_PREFIX=/opt/protobufstatic-3.0.0
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -Dprotobuf_BUILD_TESTS=OFF
                -DCMAKE_CXX_FLAGS="-Wno-deprecated-declarations"
)
