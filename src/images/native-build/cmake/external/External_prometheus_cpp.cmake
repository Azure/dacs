ExternalProject_Add(
        prometheus_cpp
        # Pin the official prometheus-cpp 1.3.0 archive with submodules by SHA-256.
        URL "https://github.com/jupp0r/prometheus-cpp/releases/download/v1.3.0/prometheus-cpp-with-submodules.tar.gz"
        URL_HASH "SHA256=62bc2cc9772db2314dbaae506ae2a75c8ee897dab053d8729e86a637b018fdb6"
        UPDATE_COMMAND ""
        PATCH_COMMAND ""
        CMAKE_ARGS 
                -DBUILD_SHARED_LIBS=OFF
                -DCMAKE_INSTALL_PREFIX=${DEP_INSTALL_DIR}
                -DENABLE_PUSH=OFF
                -DENABLE_COMPRESSION=OFF
        TEST_COMMAND ""
)
