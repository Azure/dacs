ExternalProject_Add(
        opentelemetry_cpp
        DEPENDS protobufstatic
        GIT_REPOSITORY https://github.com/open-telemetry/opentelemetry-cpp.git
        GIT_TAG v1.22.0
        GIT_SHALLOW TRUE
        UPDATE_COMMAND ""
        CMAKE_ARGS
                -DBUILD_SHARED_LIBS=OFF
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DCMAKE_INSTALL_PREFIX=${CMAKE_DEP_INSTALL_DIR}
                -DCMAKE_PREFIX_PATH=${CMAKE_DEP_INSTALL_DIR}
                -DBUILD_TESTING=OFF
                -DWITH_BENCHMARK=OFF
                -DWITH_EXAMPLES=OFF
                -DWITH_OTLP_GRPC=OFF
                -DWITH_OTLP_HTTP=ON
                -DWITH_PROMETHEUS=OFF
        TEST_COMMAND ""
)