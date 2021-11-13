find_package(Threads REQUIRED)

ExternalProject_Add(
        spdlog
        GIT_REPOSITORY "https://github.com/vaisharma/spdlog"
        GIT_TAG "v1.x"
        GIT_SHALLOW 1
        UPDATE_COMMAND ""
        PATCH_COMMAND ""
        CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${DEP_INSTALL_DIR}
        -DSPDLOG_BUILD_BENCH=OFF
        -DSPDLOG_BUILD_TESTS=OFF
        TEST_COMMAND ""
)
