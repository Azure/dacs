list(PREPEND CMAKE_PREFIX_PATH /opt/protobufstatic-3.0.0)

find_package(opentelemetry-cpp CONFIG REQUIRED
    PATHS ${CMAKE_DEP_INSTALL_DIR}
    NO_DEFAULT_PATH)