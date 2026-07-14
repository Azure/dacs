ExternalProject_Get_Property(azurestoragelite source_dir)

set(STORAGELITE_INCLUDE_DIRS ${source_dir}/include)
include_directories(${STORAGELITE_INCLUDE_DIRS})