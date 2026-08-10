
ExternalProject_Add(json
        # Pin the official nlohmann/json 3.11.3 release archive by SHA-256.
        URL "https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz"
        URL_HASH "SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d"
        INSTALL_DIR ${PROJECT_BINARY_DIR}
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR> -DJSON_BuildTests=OFF -DJSON_MultipleHeaders=ON
        UPDATE_COMMAND ""
        )
