project(StorageMetrics LANGUAGES CXX)

# --------------------------------------------------------------------------
# StorageMetrics is a static library that provides a common IMetricsStore
# interface for registering and emitting counters, gauges, and histograms.
#
# Implementations:
#   LoggerMetricsStore    - periodically aggregates metrics in project logs.
#   OtelMetricsStore      - exports metrics through the OpenTelemetry OTLP HTTP exporter.
#   CompositeMetricsStore - emits each metric to multiple enabled backends.
#
# Additional implementations can be added behind IMetricsStore without changing
# callers. A Prometheus scrape backend can be added later when required.
# --------------------------------------------------------------------------
add_library(StorageMetrics STATIC
    StorageMetrics.cpp
    MetricsStoreBase.cpp
    LoggerMetricsStore.cpp
    CompositeMetricsStore.cpp
    OtelMetricsStore.cpp
)

target_include_directories(StorageMetrics PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(StorageMetrics
    opentelemetry-cpp::metrics
    opentelemetry-cpp::otlp_http_metric_exporter)

enable_testing()
add_executable(StorageMetricsTests tests/StorageMetricsTests.cpp)
add_dependencies(StorageMetricsTests StorageMetrics)
target_link_libraries(StorageMetricsTests
    StorageMetrics
    diagnostics
    pthread
    ${GTEST_LIBRARY}
    ${GTEST_MAIN_LIBRARY}
)

add_test(NAME StorageMetricsTests COMMAND StorageMetricsTests)
