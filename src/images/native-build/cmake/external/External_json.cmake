
ExternalProject_Add(json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG "v3.10.3"
        INSTALL_DIR ${PROJECT_BINARY_DIR}
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR> -DBUILD_TESTING=0 -DJSON_MultipleHeaders=ON
        UPDATE_COMMAND ""
        )
