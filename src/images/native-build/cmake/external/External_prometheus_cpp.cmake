ExternalProject_Add(
        prometheus_cpp
        GIT_REPOSITORY "https://github.com/jupp0r/prometheus-cpp"
        GIT_TAG "v1.1.0"
        GIT_SHALLOW 1
        UPDATE_COMMAND ""
        PATCH_COMMAND ""
        CMAKE_ARGS 
                -DBUILD_SHARED_LIBS=OFF
                -DCMAKE_INSTALL_PREFIX=${DEP_INSTALL_DIR}
                -DENABLE_PUSH=OFF
                -DENABLE_COMPRESSION=OFF
        TEST_COMMAND ""
)
