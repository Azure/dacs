# CMake build utilities ( macros )

macro(nvidia_target_link_libraries TARGET)
    if(NOT AMD_BUILD)
        target_link_libraries(${TARGET} ${ARGN})
    endif()
endmacro()

macro(amd_target_link_libraries TARGET)
    if(AMD_BUILD)
        target_link_libraries(${TARGET} ${ARGN})
    endif()
endmacro()
