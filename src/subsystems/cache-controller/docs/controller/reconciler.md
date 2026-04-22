# Reconciler Architecture

## Overview

The cache controller is built around a single Kubernetes custom resource (`Cache`) and one main controller (`CacheReconciler`) that delegates to two embedded sub-reconcilers:

- **Scaling reconciler** -- determines the desired replica count based on cluster size and scaling policy.
- **Scheduling reconciler** -- handles custom pod-to-node placement and stuck pod cleanup.

Status conditions and requeue timing are accumulated via a **status helper** throughout the reconcile cycle. After all sub-reconcilers have run, a `patchStatus` call sets final conditions and writes the accumulated status in a single API call. Multiple sub-reconcilers can independently set conditions and request requeue without conflicting.

## Cache CRD

The `Cache` custom resource (group `storage.azure.com`, version `v1`) defines the desired state for a cache deployment in a namespace.

### Spec Highlights

| Field Group | Key Fields | Notes |
|---|---|---|
| Scaling | `scaling.static.numServers` or `scaling.dynamic.*` | Mutually exclusive -- exactly one must be set |
| Placement | `nodeSelectorKey`, `nodeSelectorValue` | Determines which nodes are eligible for cache pods |
| Sizing | `serverSizeInGB`, `serverThreads` | Per-pod resource configuration |
| Network | `port`, `metricsPort` | Must differ; validated by webhook |
| Storage | `storage.ephemeral` or `storage.hostPath` | Mutually exclusive |
| Eviction | `eviction.*` | Cache data eviction policy (access-time, size-based) |

### Status

| Field | Description |
|---|---|
| `conditions` | Standard `metav1.Condition` array with sub-conditions |
| `desiredServers` | Target replica count from the scaling reconciler |
| `currentServers` | Number of ready replicas from the StatefulSet |

### Conditions

The controller maintains three conditions and a scaling status field:

| Condition | Set By | True When |
|---|---|---|
| `PodsScheduled` | Scheduling reconciler | No pods are in stuck states |
| `ReplicasReady` | Main reconciler | StatefulSet ready replicas equal desired replicas |
| `Ready` | Status helper | All sub-conditions are `True` |

`Ready` is an aggregate - it is `True` only when every other condition is `True`. If any condition is `False`, `Ready` copies the first failing condition's reason and message.

Scaling stabilization state is tracked separately in `status.scaling` (see [scaling.md](scaling.md#scaling-status)). This keeps the `Ready` condition focused on health and avoids reporting a healthy cache as not-ready during a stabilization window.

## Reconcile Loop

The main reconcile loop (`CacheReconciler.Reconcile`) follows this sequence:

```
Fetch Cache CR
    ↓
[Not Found?] -> log and return (cleanup handled by finalizer)
    ↓
[Being Deleted?] -> run cleanup, remove finalizer, return
    ↓
[Missing Finalizer?] -> add finalizer, continue
    ↓
Create status helper
    ↓
Resolve desired replicas (scaling reconciler)
    ↓
Reconcile StatefulSet
    ↓
Reconcile Services
    ↓
Reconcile ConfigMap
    ↓
Run scheduling reconciler
    ↓
Set final conditions, patch status
```

### Finalizer

The controller registers a finalizer (`storage.azure.com/finalizer`) on every Cache CR. This ensures that when a Cache is deleted, the controller:

1. Removes the Cache's pod entries from the global tracker (freeing nodes for other Cache CRs).
2. Purges scaling state for the deleted Cache.
3. Removes the finalizer to allow the API server to complete deletion.

Without the finalizer, stale tracker entries would prevent freed nodes from being reused by other Cache CRs.

### Status Patch

The status helper takes a **pre-mutation snapshot** of the Cache object at the start of reconciliation. Sub-reconcilers set conditions, desired-replicas, and requeue timing on the helper throughout the loop. After all sub-reconcilers have run, `patchStatus` is called:

1. `setFinalConditions` computes `ReplicasReady` from the StatefulSet and sets `Ready` from all sub-conditions.
2. `h.Patch(ctx)` writes the accumulated status diff in a single API call.

`patchStatus` runs regardless of whether `reconcileCache` succeeded or failed, so conditions always reflect the latest observed state. If both reconciliation and the patch fail, the errors are joined.

### Requeue Timing

Multiple sub-reconcilers can request requeue independently:

- Scaling stabilization window -> requeue when window expires
- Stuck pods -> requeue after 2 minutes for re-evaluation

The status helper keeps the **shortest positive duration**, so the controller wakes at the earliest interesting time.

## Resource Reconciliation

### StatefulSet

The controller creates or updates a single StatefulSet named `cacheserver`. It renders the manifest from an embedded template (`templates/cacheserver-app.yaml`) and only mutates managed fields (`Replicas`, `Selector`, `ServiceName`, `Template`) to avoid fighting server-defaulted values.

Owner references are set so the StatefulSet is garbage-collected when the Cache CR is deleted.

### Services

Two fixed-name Services are managed:

| Service | Purpose |
|---|---|
| `cacheserver` | Client access to cache pods |
| `cacheserver-discovery` | Peer discovery (headless) |

### ConfigMap

A single ConfigMap (`cacheserver-discovery-config`) is maintained with the current server count for peer discovery.

> **Note:** Resource names are fixed constants. This enforces a **one Cache deployment per namespace** constraint.

## Multi-Cache Concurrency

Multiple Cache CRs can exist in a cluster (in different namespaces). The controller uses layered concurrency controls to keep them safe.

### Controller-Runtime Serialization

Controller-runtime's work queue guarantees that reconciles for the **same** Cache CR (by NamespacedName) are serialized — only one `Reconcile` call per CR runs at a time. No explicit `MaxConcurrentReconciles` is set, so the controller defaults to a single worker. The code is written to be safe if concurrency is increased later.

### Panic Recovery

The controller is configured with `RecoverPanic: true` via `controller.Options`. If a reconcile panics, controller-runtime catches the panic, logs the stack trace, and re-queues the item instead of crashing the entire pod. This improves resilience in production where an unexpected nil dereference or similar bug in one reconcile should not take down the controller manager.

### Scaling

Each Cache CR computes its desired replica count independently. The scaling reconciler lists schedulable nodes matching **that cache's** `NodeSelectorKey`/`NodeSelectorValue` and applies its own percentage, min, and max settings. There is no cross-cache subtraction at the scaling layer — if two caches use the same node selector they both see the same node count. Conflict resolution happens at scheduling time instead.

The normalizer stores per-cache stabilization state in a `sync.Map` keyed by `types.NamespacedName`. Because each CR accesses a different key, there is no lock contention between concurrent reconciles.

When a node changes (labels, schedulability), `MapNodeToCaches` lists all Cache CRs and enqueues every one whose selector matches the node, so each affected cache re-evaluates its replica count.

### Tracker and Scheduling

The tracker is the shared mutable structure that prevents two Cache CRs from placing pods on the same node. It maintains a `PodsByCacheKey` map (`namespace/name` → pod list) protected by a `sync.Mutex`.

The scheduling reconciler calls `Tracker.UpdateAndSchedule`, which acquires the tracker mutex internally and holds it through the full update-then-schedule sequence:

```go
instructions := r.Tracker.UpdateAndSchedule(ctx, r.Client, cacheKey, nodes, pods)
```

Inside the locked section, `getNodesWithPods` iterates **all** entries in `PodsByCacheKey` to build a global set of occupied nodes. The scheduler then only considers unoccupied nodes for the current cache's pending pods. The lock is released before executing the returned instructions against the Kubernetes API.

The tracker is pure in-memory state — it is not persisted. On controller restart the maps start empty and are rebuilt from the Kubernetes API on the first reconcile of each Cache CR.

### Deletion Cleanup

When a Cache CR is deleted, the finalizer handler:

1. Calls `Scaler.Deleted` → purges the normalizer's `sync.Map` entry for that key.
2. Calls `Tracker.RemoveCacheKey` → deletes the cache's pod list from `PodsByCacheKey` (under its own mutex lock).

This immediately frees the nodes so other Cache CRs can schedule onto them.

## Watches and Predicates

The controller watches several resource types to trigger reconciliation:

| Resource | Watch Type | Predicate | Why |
|---|---|---|---|
| `Cache` | Primary (`For`) | `GenerationChangedPredicate` | Spec changes bump generation |
| `StatefulSet` | Owned (`Owns`) | Generation, label, or replica-status changes | Detect scaling progress and rollout status |
| `Service` | Owned (`Owns`) | Generation or label changes | Detect service drift |
| `ConfigMap` | Owned (`Owns`) | Generation or label changes | Detect config drift |
| `Node` | External (`Watches`) | Label or schedulability changes | Trigger dynamic scaling recalculation |
| `Pod` | External (`Watches`) | Phase, node, deletion, or readiness changes | Trigger scheduling reconciliation |

### Why Pods Are Watched Externally

Cache pods are **grandchildren** of the Cache CR (Cache → StatefulSet → Pod). The `Owns` mechanism only watches direct children, so pods are watched with a `MapPodToCaches` handler. Each pod carries a `dacs.azure.com/cache-name` label (propagated through the StatefulSet pod template), so the handler reads the label to enqueue exactly the owning Cache CR — no API call required.

## Webhooks

### Defaulting

The mutating webhook fills in sensible defaults for omitted fields:

- Ports: `9065` / `9096`
- Node selector: `dacs.azure.com/processing-unit: gpu`
- Scaling: defaults to static with 1 server if neither static nor dynamic is specified
- Storage: defaults to hostPath at `/var/lib/ssd/cacheserver`
- Eviction: access-time eviction enabled with 30-day threshold

### Validation

The validating webhook enforces:

- Required fields are present
- Numeric fields meet minimums
- Ports are 1–65535 and differ from each other
- Exactly one of static/dynamic scaling
- Static `numServers >= 1`; dynamic `percentageOfNodesToUse` 1-100
- Exactly one of ephemeral/hostPath storage
- Duration fields parse correctly and are within bounds
- Percentage strings are 0–100
- Eviction `sizeTarget < sizeThreshold`

## Implementation Reference

| Component | File | Purpose |
|---|---|---|
| Main reconciler | `internal/controller/crd_controller.go` | Reconcile loop, finalizer, resource reconciliation |
| Scaling reconciler | `internal/scaling/scaling_controller.go` | Desired replica computation, delegates to calculator and normalizer |
| Scheduling reconciler | `internal/scheduling/reconciler.go` | Pod-to-node placement, stuck pod cleanup, `MapPodToCaches` handler |
| Tracker | `internal/management/tracker.go` | Global pod-to-node tracking across Cache CRs |
| Scheduler | `internal/management/scheduler.go` | Node selection logic for pod placement |
| Cache CRD types | `api/v1/cache_types.go` | Spec, status, and condition types |
| Condition constants | `api/v1/conditions.go` | Condition type string constants |
| Defaults | `api/v1/defaults.go` | Default constants |
| Webhooks | `api/v1/cache_webhook.go` | Defaulting and validation |
| Status helper | `internal/statushelper/statushelper.go` | Condition accumulation, requeue timing, and status patching |
| Labels | `internal/controller/labels.go` | Label key constants |
| Predicates | `internal/controller/predicates.go` | StatefulSet and Pod change predicates |
| Node predicate | `internal/controller/node_predicate.go` | Label and schedulability change detection |
| Resource templates | `internal/controller/templates/` | Embedded YAML templates for StatefulSet, Services, and ConfigMap |
