# DACS post-installation troubleshooting guides (TSGs)

Use these guides for basic issues after DACS has been installed. Each issue has
its own Symptoms, Description, Steps to Mitigate, Verification Steps, and
Contact Information sections.

| Symptom | Troubleshooting guide |
| --- | --- |
| Reads become slow or throughput drops | [Slow reads or low throughput](tsg/slow-reads.md) |
| Repeated reads continue to miss the cache | [Repeated cache misses](tsg/cache-misses.md) |
| Cache writes fail or nodes report disk pressure | [Cache capacity or disk pressure](tsg/cache-capacity.md) |
| Replica count does not change as expected | [Unexpected cache scaling](tsg/cache-scaling.md) |
| Cache access degrades after a node restart or replacement | [Recovery after node disruption](tsg/node-recovery.md) |
| Remote Blob requests fail with authentication or access errors | [Remote storage access failure](tsg/storage-access.md) |
| Writes fail or data is absent from remote storage | [Client write failure or unexpected persistence](tsg/client-writes.md) |
| Dashboards or client metrics are missing | [Missing metrics](tsg/missing-metrics.md) |
| Reads report checksum mismatches | [Checksum validation failure](tsg/checksum-failure.md) |

For initial setup failures, use the
[installation troubleshooting index](installation-troubleshooting.md).
Existing guides for [client connectivity](tsg/cache-connectivity.md),
[CacheServer startup](tsg/cacheserver-startup.md), and
[controller startup](tsg/controller-startup.md) also apply to runtime incidents.

## Before making changes

- Record the failure time, affected workloads, recent configuration changes,
  node maintenance, and relevant logs/events before restarting anything.
- Examples use namespace `dacs-system`, Cache `cache-sample`, and
  `namePrefix=dacs`. Substitute actual names and replace angle-bracket
  placeholders. Each guide includes the variables it needs.
- Change Helm-managed settings through the original values file and pinned chart
  version. Direct changes to generated StatefulSets may be reconciled away.
- Cache eviction, pod replacement, scaling, and node replacement can remove
  cached data. Do not treat cache-only writes or pending asynchronous uploads as
  durable Blob data. Confirm the authoritative copy before destructive actions.
- Do not delete Cache resources, CRDs, namespaces, or cache directories to
  diagnose an issue. Do not disable checksum, TLS, or authorization checks.
- Redact credentials, tokens, SAS query strings, and sensitive paths from
  diagnostics. Use the existing
  [escalation checklist](installation-troubleshooting.md#escalation-checklist).

## Verification

Verify the affected application operation, not just pod status. Where
appropriate, also run the
[quickstart smoke test](quickstart.md#run-the-end-to-end-smoke-test).
It verifies cache upload/download and checksum validation; it does not verify
remote Blob authorization, application write durability, or production performance.
