ExternalProject_Add(
        azurestoragelite
        # Pin the selected azure-storage-cpplite commit archive by SHA-256.
        URL "https://github.com/Azure/azure-storage-cpplite/archive/33a807a3984681a604c41659a4a2030ea81194e7.tar.gz"
        URL_HASH "SHA256=c781b7e6a3d7706657b024191cbaa35b7a06fce9b3d0287836eee321a9ada18f"
        CMAKE_ARGS 
                -DBUILD_SHARED_LIBS=OFF
                -DCMAKE_INSTALL_PREFIX=/opt/azure-storage-cpplite-0.3.0
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DCMAKE_CXX_FLAGS="-Wno-deprecated-declarations"
)
