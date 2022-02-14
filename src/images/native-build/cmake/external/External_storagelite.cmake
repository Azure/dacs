ExternalProject_Add(
        azurestoragelite
        GIT_REPOSITORY https://github.com/Azure/azure-storage-cpplite.git
        GIT_TAG "v0.3.0"
        INSTALL_DIR ${PROJECT_BINARY_DIR}
        CMAKE_ARGS 
                -DBUILD_SHARED_LIBS=ON
                -DCMAKE_INSTALL_PREFIX=${DEP_INSTALL_DIR}
)