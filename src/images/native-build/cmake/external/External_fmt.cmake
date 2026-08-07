ExternalProject_Add(
        fmt
        # Pin the official fmt 7.1.0 release archive by SHA-256.
        URL "https://github.com/fmtlib/fmt/releases/download/7.1.0/fmt-7.1.0.zip"
        URL_HASH "SHA256=308af4e36ee3ab527b51014a2a5d862682c84f5d16f7a597aea34c84853cbcb0"
        UPDATE_COMMAND ""
        CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${CMAKE_DEP_INSTALL_DIR}
        -DFMT_DOC=OFF
        -DFMT_TEST=OFF
        TEST_COMMAND ""
)
