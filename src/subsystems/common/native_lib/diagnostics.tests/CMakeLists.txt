project(diagnostics.tests)

# Add test discovery and execution
enable_testing()
add_executable(diagnostics_tests
        LoggingApiTest.cpp
        FileLoggerTest.cpp
        TestMain.cpp)

target_include_directories(diagnostics_tests PRIVATE  ../diagnostics)
target_include_directories(diagnostics_tests PRIVATE ${DEP_INSTALL_DIR}/include)
add_dependencies(diagnostics_tests diagnostics ${GMOCK_LIBRARY} ${GMOCK_MAIN_LIBRARY} ${GTEST_LIBRARY} ${GTEST_MAIN_LIBRARY})
target_link_libraries(diagnostics_tests diagnostics -lstdc++fs  ${GMOCK_LIBRARY} ${GMOCK_MAIN_LIBRARY} ${GTEST_LIBRARY} ${GTEST_MAIN_LIBRARY})
add_test(NAME diagnostics_tests COMMAND diagnostics_tests)