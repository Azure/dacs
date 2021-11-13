ExternalProject_Get_Property(spdlog source_dir)

set(SPDLOG_INCLUDE_DIRS ${source_dir}/include)
include_directories(${SPDLOG_INCLUDE_DIRS})