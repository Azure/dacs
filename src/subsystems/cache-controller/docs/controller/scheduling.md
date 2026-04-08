# Scheduling

## Overview

The scheduling reconciler (`internal/scheduling/`, `internal/management/`) handles custom pod-to-node placement for cache server pods. It bypasses the default Kubernetes scheduler to enforce cache-specific constraints:

- **One cache pod per node** -- no two cache pods (from any Cache CR) share a node.
- **Cache affinity** -- pods prefer returning to their last-known node to preserve local cache data.
- **Stuck pod management** -- pods that remain in bad states are detected, reported, and eventually deleted.

## Architecture

Scheduling is split into three layers:

| Layer | Location | Responsibility |
|---|---|---|
| Scheduling reconciler | `internal/scheduling/scheduling.go` | Orchestrates: list resources, update tracker, run scheduler, execute instructions |
| Scheduler | `internal/management/scheduler.go` | Core algorithm: decide what to bind, delete, or skip |
| Tracker | `internal/management/tracker.go` | Global in-memory state: pod-to-node mapping, stuck detection, node health |

## Tracker

The tracker is a **singleton** shared across all Cache CR reconciles. It maintains the global cluster state that the scheduler needs to make decisions.

### Data Model

```
Tracker
├── CurrentNodes      map[nodeName] -> nodeInfo (labels, schedulability, stuck events)
├── PodMap            map[podName]  -> podInfo  (state, node, timestamps)
├── PodsByCacheKey    map[cacheKey] -> []Pod    (per-CR pod list)
└── mu                sync.Mutex               (protects concurrent access)
```

### Global vs Per-Cache Scope

The tracker serves two overlapping purposes:

| Operation | Scope | Why |
|---|---|---|
| `GetNodesWithPods()` | **Global** -- iterates all cache keys | Prevents two different Cache CRs from scheduling pods onto the same node |
| `GetCurrentPods(cacheKey)` | **Per-Cache** -- explicit key parameter | Each reconcile only sees its own pods |
| `GetStuckPods(cacheKey)` | **Per-Cache** | Stuck detection and deletion is per-Cache |
| `UpdateTracker(cacheKey)` | **Per-Cache** | Updates the `PodsByCacheKey[cacheKey]` entry |

### Locking

The tracker encapsulates locking inside `UpdateAndSchedule`, which acquires the mutex, updates state, runs the scheduler, and returns instructions. This ensures that concurrent reconciles (for different Cache CRs) see a consistent view of global state without exposing lock/unlock to callers.

```go
instructions := r.Tracker.UpdateAndSchedule(ctx, r.Client, cacheKey, nodes, pods)
```

### Cleanup

When a Cache CR is deleted, the finalizer calls `RemoveCacheKey(cacheKey)` to remove all pod entries for that Cache. Without this, stale entries would make nodes appear occupied even after the Cache and its pods are gone.

### Stuck Pod Detection

Pods are considered "stuck" when they remain in a bad state for longer than the configured threshold:

| Bad State | Description |
|---|---|
| Pending | Pod cannot be scheduled |
| Failed | Container exited with error |
| Unknown | Node lost contact |
| Terminating | DeletionTimestamp set but pod still exists |
| NotReady | Running but not passing readiness probes |

**Thresholds:**

- `PodStuckThresholdDuration` -- configurable; how long a pod must be stuck before it's eligible for deletion.
- `PodStuckEventRetentionDuration` -- 24 hours; how long stuck events are remembered on a node.
- `SuccessivePodStuckEventThreshold` -- 3 events; a node with this many recent stuck events is considered unhealthy.

**Node health inference:** If a pod runs successfully on a node for the full threshold duration, the node's stuck-event history is cleared. This allows nodes to recover from transient issues.

## Scheduler Algorithm

The `Schedule()` function produces a list of instructions (bind or delete) that the scheduling reconciler executes against the Kubernetes API.

### Execution Order

```
Get globally occupied nodes
    ↓
Delete truly stuck pods (-> delete instructions)
    ↓
For each pending pod:
    └── Find target node -> bind instruction
```

### Node Selection (`findNodeToSchedule`)

When a pending pod needs a node, the algorithm selects one in preference order:

1. **Last-known node** -- if the pod was previously running on a node that is still free, schedulable, and healthy, it returns there. This preserves local cache data.
2. **Any healthy free node** -- a node with no pods from any Cache CR and no recent stuck events.
3. **Any free matching node** -- a node with no pods but possibly with recent stuck events (less preferred).
4. **No node available** -- the pod remains pending. The instruction set is empty for this pod.

"Free" means no pod from **any** Cache CR is on that node -- this is the cross-Cache scheduling protection.

### Cross-Cache Node Protection

Multiple Cache CRs may exist in different namespaces. The tracker maintains pods per cache key globally, so `GetNodesWithPods()` returns the union of all occupied nodes. This prevents:

- Cache A scheduling a pod onto a node already used by Cache B.
- Stale entries from a deleted Cache blocking nodes (solved by the finalizer cleanup).

### Instruction Types

| Type | When Generated | Effect |
|---|---|---|
| `BindInstruction` | Pending pod + available node | Creates a Pod Binding to assign the pod to the node |
| `DeleteInstruction` | Stuck pod or long-term eviction | Deletes the pod |

Instructions are executed by `ExecuteSchedulerInstructionsOnK8s` which makes the actual Kubernetes API calls.

## Scheduling Reconciler Flow

The scheduling reconciler (`scheduling.Reconciler.Reconcile`) runs after scaling and resource reconciliation:

1. **List nodes** via the Kubernetes API (all nodes, not just matching ones -- the scheduler filters).
2. **Get owned pods** -- finds the StatefulSet owned by this Cache, then lists pods owned by that StatefulSet. This scoping prevents mixing pods from different workloads.
3. **UpdateAndSchedule** -- atomically locks the tracker, updates state, runs the scheduler, and returns instructions.
4. **Execute instructions** against the Kubernetes API.
8. **Set conditions**:
   - If any pods are in stuck states -> `PodsScheduled=False`, requeue after 2 minutes.
   - Otherwise -> `PodsScheduled=True`.

## Pod Watches

Cache pods are grandchildren of the Cache CR (Cache -> StatefulSet -> Pod), so the standard `Owns` mechanism doesn't apply. Instead, the controller watches pods directly with:

- **`cacheserverPodPredicate`** -- only reacts to pods labeled `app=cacheserver` with scheduling-relevant changes (phase, node assignment, deletion timestamp, container readiness).
- **`MapPodToCaches`** -- reads the `dacs.azure.com/cache-name` label from the pod and enqueues the owning Cache CR directly (no API call).

## Implementation Reference

| Component | File | Purpose |
|---|---|---|
| Scheduling reconciler | `internal/scheduling/scheduling.go` | Orchestration, pod ownership |
| Scheduler | `internal/management/scheduler.go` | Core scheduling algorithm, node selection, eviction |
| Tracker | `internal/management/tracker.go` | Global state, per-CR pods, stuck detection, node health |
| Pod predicate | `internal/controller/pod_predicate.go` | Filters scheduling-relevant pod events |
| Kubernetes helpers | `internal/management/kubernetes_cache.go` | Node/pod listing from Kubernetes API |
