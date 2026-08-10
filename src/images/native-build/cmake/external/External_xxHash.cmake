ExternalProject_Add(
        xxHash
        # Pin the xxHash 0.8.0 tag archive by SHA-256.
        URL "https://github.com/Cyan4973/xxHash/archive/refs/tags/v0.8.0.tar.gz"
        URL_HASH "SHA256=7054c3ebd169c97b64a92d7b994ab63c70dd53a06974f1f630ab782c28db0f4f"
        UPDATE_COMMAND ""
        PATCH_COMMAND ""
        CMAKE_ARGS 
                -DBUILD_SHARED_LIBS=OFF
                -DCMAKE_INSTALL_PREFIX=${DEP_INSTALL_DIR}
        TEST_COMMAND ""
        SOURCE_SUBDIR "cmake_unofficial"
)

# line 13 : This repo has CMakeLists.txt in /cmake_unofficial - not root
