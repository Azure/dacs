project(liveness_probe LANGUAGES CXX C)
cmake_minimum_required(VERSION 3.10)

set(CMAKE_REMOTE_PUBLISH_DIRECTORY "${CMAKE_REMOTE_PUBLISH_DIRECTORY}-storage/storage")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}-storage/storage")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}-storage/storage")

message("Current CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message("Current CMAKE_LIBRARY_OUTPUT_DIRECTORY from ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
message("Current CMAKE_REMOTE_PUBLISH_DIRECTORY ${CMAKE_REMOTE_PUBLISH_DIRECTORY}")

add_executable(LivenessProbe Liveness.cpp)

add_dependencies(LivenessProbe CacheClientLib StorageCommon)

target_link_libraries(LivenessProbe CacheClientLib StorageCommon)

add_custom_command(
    TARGET LivenessProbe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:LivenessProbe> ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})