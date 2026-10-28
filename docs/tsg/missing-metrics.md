# DACS Missing Metrics

## Symptoms

Dashboards show no DACS data, Prometheus targets are absent/down, or expected
client hit/miss and latency metrics do not appear in logs or an OTLP collector.

## Description

Controller, CacheServer, and client metrics are separate surfaces. Controller
metrics use a protected HTTPS endpoint (default port `8443`); CacheServers have
a separate metrics port (default `9096`). Client logging and OTLP export have
their own settings and intervals. Enabling one surface does not configure all
the others.

## Steps to Mitigate

1. Identify which surface is missing and verify its workload is healthy:

   ```bash
   export NAMESPACE="dacs-system"
   export RELEASE="dacs"
   helm get values "$RELEASE" -n "$NAMESPACE" --all
   kubectl get pods,services -n "$NAMESPACE"
   ```

   Review values locally and redact secrets before sharing.
2. For controller metrics, verify `metrics.enable`, its configured port, and the
   Service's ready endpoints (names assume `namePrefix=dacs`):

   ```bash
   kubectl get service dacs-manager-metrics-service -n "$NAMESPACE"
   kubectl get endpointslices -n "$NAMESPACE" \
     -l kubernetes.io/service-name=dacs-manager-metrics-service
   ```

   Inspect collector errors for TLS or RBAC failures. Use the approved trust
   and scrape identity; do not disable authentication or certificate validation.
3. If using the chart's ServiceMonitor, verify the Prometheus Operator CRDs
   exist and `prometheus.enable` is enabled:

   ```bash
   kubectl get crd servicemonitors.monitoring.coreos.com
   kubectl get servicemonitors -n "$NAMESPACE"
   ```

   A missing CRD means the optional integration is unavailable. Also check the
   Prometheus instance's namespace/label selectors and target discovery.
4. For CacheServer metrics, check `cache.metricsPort` and the actual scrape
   target/port rather than scraping the controller endpoint for cache data.
5. For client metrics, review `STORAGE_METRIC_LOGGING_ENABLED` and
   `STORAGE_METRIC_OTEL_ENABLED` before process startup. Logging defaults to
   enabled at 15-minute intervals; OTLP defaults to disabled with a 5-minute
   export interval when enabled. Verify the OTLP HTTP endpoint and collector
   reachability from the client.
6. Use the [client metrics configuration](../../src/subsystems/storage/storage_intercept/README.md#configure-client-metrics)
   and generate a small known workload. Allow the configured interval to elapse;
   an empty dashboard immediately after startup is not sufficient evidence of
   export failure.

## Verification Steps

Confirm the intended scrape target is healthy or client exports reach the
collector. Run known reads/writes and verify relevant counters or latency data
update within the configured interval. Check that dashboards select the right
namespace, service, process, and time range.

## Contact Information

Contact the monitoring administrator for collector, trust, and scrape-identity
configuration. For DACS help, use [SUPPORT.md](../../SUPPORT.md) and the
[escalation checklist](../installation-troubleshooting.md#escalation-checklist).
