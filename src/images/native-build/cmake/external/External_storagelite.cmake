ExternalProject_Add(
        azurestoragelite
        GIT_REPOSITORY https://github.com/Azure/azure-storage-cpplite.git
        GIT_TAG "33a807a3984681a604c41659a4a2030ea81194e7"
        CMAKE_ARGS 
                -DBUILD_SHARED_LIBS=OFF
                -DCMAKE_INSTALL_PREFIX=/opt/azure-storage-cpplite-0.3.0
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DCMAKE_CXX_FLAGS="-Wno-deprecated-declarations"
)
