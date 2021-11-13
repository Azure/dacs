ExternalProject_Add(
        xxHash
        GIT_REPOSITORY "https://github.com/Cyan4973/xxHash"
        GIT_TAG "v0.8.0"
        GIT_SHALLOW 1
        UPDATE_COMMAND ""
        PATCH_COMMAND ""
        CMAKE_ARGS 
                -DBUILD_SHARED_LIBS=OFF
                -DCMAKE_INSTALL_PREFIX=${DEP_INSTALL_DIR}
        TEST_COMMAND ""
        SOURCE_SUBDIR "cmake_unofficial"
)

# line 13 : This repo has CMakeLists.txt in /cmake_unofficial - not root
