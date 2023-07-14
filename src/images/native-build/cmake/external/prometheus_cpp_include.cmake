
ExternalProject_Get_Property(prometheus_cpp source_dir)

set(PROMETHEUS_CPP_INCLUDE_DIRS ${source_dir}/include)
include_directories(${PROMETHEUS_CPP_INCLUDE_DIRS})