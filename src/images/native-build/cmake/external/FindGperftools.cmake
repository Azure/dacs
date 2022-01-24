find_library(GPERFTOOLS_PROFILER
  NAMES profiler
  HINTS ${Gperftools_ROOT_DIR}/lib)

find_path(GPERFTOOLS_INCLUDE_DIR
  NAMES gperftools/profiler.h
  HINTS ${Gperftools_ROOT_DIR}/include)

set(GPERFTOOLS_LIBRARIES ${GPERFTOOLS_PROFILER})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  Gperftools
  DEFAULT_MSG
  GPERFTOOLS_LIBRARIES
  GPERFTOOLS_INCLUDE_DIR)

mark_as_advanced(
  Gperftools_ROOT_DIR
  GPERFTOOLS_PROFILER
  GPERFTOOLS_LIBRARIES
  GPERFTOOLS_INCLUDE_DIR)

# create IMPORTED targets
if (Gperftools_FOUND AND NOT TARGET gperftools::profiler)
  add_library(gperftools::profiler SHARED IMPORTED)
  set_target_properties(gperftools::profiler PROPERTIES
    IMPORTED_LOCATION ${GPERFTOOLS_PROFILER}
    INTERFACE_INCLUDE_DIRECTORIES "${GPERFTOOLS_INCLUDE_DIR}")
endif()
