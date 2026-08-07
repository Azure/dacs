# Scaling

## Overview

The scaling reconciler (`internal/scaling/`) determines the desired replica count
for the cache StatefulSet. It supports two mutually exclusive modes:

- **Static** -- a fixed replica count set in `spec.scaling.static.numServers`.
- **Dynamic** -- a percentage-based formula that tracks cluster size and adjusts
  replicas automatically.

Dynamic scaling includes tolerance bands and stabilization windows to prevent
unnecessary churn during cluster changes.

For production installation and operational configuration, see the
[DACS deployment and operations guide](../../../../../user-guide.md).

## Static Scaling

When `spec.scaling.static` is configured, the desired replica count is always
`numServers`. The normalizer state is purged (no stabilization tracking needed)
and the `status.scaling` field is set to `nil` (stabilization tracking is not applicable).

## Dynamic Scaling

When `spec.scaling.dynamic` is configured, the controller computes the desired
replica count on every reconcile using this pipeline:

```
List schedulable nodes matching nodeSelector
    |
    v
Calculate raw desired = floor(nodeCount * percentageOfNodesToUse / 100)
    |
    v
Apply server bounds (minServers / maxServers)
    |
    v
Normalize (tolerance + stabilization window)
    |
    v
Return stabilized replicas + RequeueAfter
```

### Calculator

The core formula is:

```
desired = floor(effectiveNodeCount * percentageOfNodesToUse / 100)
```

The replica count will always be at least 1 (the cache always has at least one
server), unless minServers forces a floor-to-zero (see below).

#### Server Bounds

Optional `minServers` and `maxServers` fields clamp the calculator output:

- **maxServers:** If set, the desired count is capped to `min(desired, maxServers)`.
- **minServers:** If set and `desired` is between 1 and `minServers - 1`
  (exclusive), the desired count is floored to 0. This implements an
  "all-or-nothing" minimum: the cache either runs at least `minServers`
  replicas or shuts down entirely. This is useful when running fewer than
  `minServers` replicas would actually be less efficient than pulling from blob.

Bounds are applied in order: maxServers cap first, then minServers floor.
Both fields are optional pointers; when nil, the corresponding bound is skipped.

### Configuration

Stabilization parameters are internal operator configuration, not exposed in the
Cache CRD. They are set in the controller's config file
(`cache-controller-config.yaml`) or via environment variables.

```yaml
# cache-controller-config.yaml
scaleUpStabilizationWindow: 15m
scaleDownStabilizationWindow: 15m
scaleUpTolerancePercent: 10
scaleDownTolerancePercent: 10
```

Environment variable overrides follow the `CACHE_CONTROLLER_` prefix convention
(e.g., `CACHE_CONTROLLER_SCALE_UP_STABILIZATION_WINDOW=20m`).

The Cache CRD exposes only the core scaling knobs:

| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| `percentageOfNodesToUse` | `int32` | 1-100 | _(required)_ | Percentage of schedulable matching nodes to use |
| `minServers` | `*int32` | 1-499 | _(nil)_ | Minimum replica count; if desired falls below this, floor to 0 |
| `maxServers` | `*int32` | 1-499 | _(nil)_ | Maximum replica count cap; must be >= minServers |

Operator-level config (applies to all Cache CRs):

| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| `scaleUpStabilizationWindow` | `time.Duration` | >= 0 | `15m` | How long the normalizer requires desired > current before scaling up |
| `scaleDownStabilizationWindow` | `time.Duration` | >= 0 | `15m` | How long the normalizer requires desired < current before scaling down |
| `scaleUpTolerancePercent` | `int32` | 0--100 | `10` | Percentage change threshold below which scale-up is suppressed |
| `scaleDownTolerancePercent` | `int32` | 0--100 | `10` | Percentage change threshold below which scale-down is suppressed |

These values are validated at controller startup. The normalizer reads them from
`config.Config`, which is injected via the scaling controller constructor.

## Normalization Algorithm

The normalizer smooths scaling decisions to avoid flapping. It applies two
stages of smoothing before the controller acts on a raw desired count.

### Stage 1: Tolerance Check

The tolerance check prevents the system from scaling in response to negligible
proportional changes in the node pool. This avoids unnecessary StatefulSet
churn when the cluster is near a scaling boundary.

```
ratio = rawDesired / currentReplicas

if scaling up (rawDesired > currentReplicas):
    if ratio <= 1.0 + scaleUpTolerance:
        suppress -> rawDesired = currentReplicas

if scaling down (rawDesired < currentReplicas):
    if ratio >= 1.0 - scaleDownTolerance:
        suppress -> rawDesired = currentReplicas
```

**Example:** With the default tolerance of `10%` and `currentReplicas = 10`:

- A raw desired count of `11` (10% increase) is **suppressed** -- the ratio
  `1.1` does not exceed `1.0 + 0.1`.
- A raw desired count of `12` (20% increase) is **allowed** -- the ratio `1.2`
  exceeds `1.1`.

Tolerance is applied *per direction*, so scale-up and scale-down can have
different sensitivity.

### Stage 2: Stabilization Window

After the tolerance check, the (possibly suppressed) desired count enters the
stabilization window. The normalizer tracks two timestamps per Cache CR:

- `lastAtOrAbove` -- the most recent time the desired count was >= current
- `lastAtOrBelow` -- the most recent time the desired count was <= current

Scaling is allowed only when the desired count has been **continuously** on one
side of the current count for the full window duration:

- **Scale-up:** allowed when `lastAtOrBelow` is older than `upWindow`
  (desired has been continuously above current for the full window).
- **Scale-down:** allowed when `lastAtOrAbove` is older than `downWindow`
  (desired has been continuously below current for the full window).

When allowed, scaling **jumps directly** to the latest desired value -- no
intermediate steps.

```
if scale-up (desired > current) and upWindow > 0:
    age = now - lastAtOrBelow
    if age < upWindow:
        result = current             (hold)
        requeueAfter = upWindow - age
    else:
        result = desired             (jump)

if scale-down (desired < current) and downWindow > 0:
    age = now - lastAtOrAbove
    if age < downWindow:
        result = current             (hold)
        requeueAfter = downWindow - age
    else:
        result = desired             (jump)
```

When the normalizer is holding back a change but the computed `requeueAfter`
is zero or negative (due to clock skew or rounding), it is clamped to 1
second. This ensures the controller always re-evaluates rather than treating
a held-back change as needing no requeue.

When `desired == currentReplicas` (including tolerance-suppressed values), both
timestamps are updated -- this correctly reflects that the system was recently
at the current level.

**First observation:** When a Cache CR is seen for the first time (no stored
state), the normalizer stores initial state and scales immediately. This
covers initial deployment and controller restarts.

**Spec changes:** The normalizer tracks the last-seen `DynamicScaleConfig`.
When `recordObservation` detects that the scaling spec has changed (via
`equality.Semantic.DeepEqual`), it zeroes both timestamps. On the next
`stabilize` call, the zeroed timestamps are detected and scaling proceeds
immediately. This ensures user-initiated configuration changes take effect
without waiting for a prior stabilization window to expire.

## Worked Examples

### Scale-down: node pool shrinking

Consider a Cache with 10 replicas, default settings (tolerance = 10%), a 5
minute stabilization window, and a node pool scaling down:

| Time | Raw desired | After tolerance | lastAtOrAbove | Stabilized | Action |
|------|-------------|-----------------|---------------|------------|--------|
| 0m | 10 | 10 | t=0 | 10 | Seed |
| 1m | 9 | 10 (suppressed) | t=0 | 10 | Tolerance band |
| 2m | 8 | 8 | t=0 | 10 | Held (age 2m < 5m) |
| 3m | 7 | 7 | t=0 | 10 | Held (age 3m < 5m) |
| 4m | 7 | 7 | t=0 | 10 | Held (age 4m < 5m) |
| 5m | 7 | 7 | t=0 | 7 | **Jump** (age 5m >= 5m) |

At t=0, `recordObservation` sets both `lastAtOrAbove` and `lastAtOrBelow`
to t=0 (raw 10 == current 10, satisfies both `>=` and `<=`).

At t=1m, the raw value of 9 is within the 10% tolerance band and is
suppressed to 10. However, `recordObservation` uses the raw value (9),
which only satisfies `<=`, so only `lastAtOrBelow` updates to t=1m.
`lastAtOrAbove` stays at t=0.

From t=2m onwards, the raw desired of 8 and 7 exceeds tolerance (20%+ drop)
but `lastAtOrAbove` is only 2-4 minutes old -- the window hasn't been served.

At t=5m, `lastAtOrAbove` is 5 minutes old. The desired count has been
continuously below current for the full window. A single scale-down to 7
occurs -- **one ring rebalance** instead of stepping through 9, 8.

### Scale-up: node pool expanding

Consider a Cache with 5 replicas (50% of 10 nodes). The cluster scales the pool to 20
nodes over 4 minutes, with new nodes becoming Ready one by one:

| Time | Nodes | Raw desired | After tolerance | lastAtOrBelow | Stabilized | Action |
|------|-------|-------------|-----------------|---------------|------------|--------|
| 0m | 10 | 5 | 5 | t=0 | 5 | Seed (first observation) |
| 1m | 12 | 6 | 6 | t=0 | 5 | Held (age 1m < 5m) |
| 2m | 14 | 7 | 7 | t=0 | 5 | Held (age 2m < 5m) |
| 3m | 16 | 8 | 8 | t=0 | 5 | Held (age 3m < 5m) |
| 4m | 18 | 9 | 9 | t=0 | 5 | Held (age 4m < 5m) |
| 5m | 20 | 10 | 10 | t=0 | 10 | **Jump** (age 5m >= 5m) |

From t=1m to t=4m, each arriving node increases the raw desired count, but
`lastAtOrBelow` stays at t=0 (the seed). Since `desired > current` at every
step, `lastAtOrBelow` is never refreshed. At t=5m the window expires and
scaling jumps directly from 5 to 10 -- **one ring rebalance** for the entire
node pool expansion.

### Node pool upgrade (no scaling)

Many managed Kubernetes upgrades add surge nodes temporarily, then drain old
nodes.
With `maxSurge=1` on a 10-node pool, the schedulable count oscillates between
10 and 11 throughout the upgrade:

| Time | Event | Schedulable | Raw desired | After tolerance | Action |
|------|-------|-------------|-------------|-----------------|--------|
| 0m | Steady | 10 | 5 | 5 | No change |
| 0m30s | Surge +1 | 11 | 6 | 5 (suppressed) | 20% up, within tolerance |
| 1m | Drain -1 | 10 | 5 | 5 | No change |
| 2m | Surge +1 | 11 | 6 | 5 (suppressed) | Tolerance suppresses |
| 2m30s | Drain -1 | 10 | 5 | 5 | No change |
| ... | (repeat x10) | 10-11 | 5-6 | 5 | Tolerance suppresses |

The upgrade produces **zero scaling operations**. Here's why:

1. When a surge node appears (11 schedulable -> raw desired 6), that's a 20%
   increase from 5. With 10% tolerance, `6/5 = 1.2 > 1.1`, so it *would*
   pass tolerance -- but the stabilization window still holds because
   `lastAtOrBelow` was just updated (we were at 5 moments ago).

2. When the old node is drained (back to 10 -> desired 5 = current), both
   timestamps update to now.

3. The cycle repeats every ~2 minutes. Each time desired returns to current,
   both timestamps reset. The window never completes because the desired count
   keeps crossing back.

For larger pools (100 nodes, `maxSurge=10`), tolerance alone handles it: 110
nodes -> 55 desired is a 10% increase from 50, which falls within the tolerance
band and is suppressed entirely.

## Reconciler Integration

### Event Sources

The cache controller reconciles in response to several types of events, plus
timer-based requeue:

| Source | Mechanism | When it fires |
|--------|-----------|---------------|
| Cache CR changes | `For(&Cache{})` with `GenerationChangedPredicate` | User edits `spec` (generation increments) |
| Owned StatefulSet | `Owns(&StatefulSet{})` with generation, label, or status predicates | Replica count changes, pods become ready |
| Owned Service / ConfigMap | `Owns(&Service{})`, `Owns(&ConfigMap{})` with generation or label predicates | Spec or label changes |
| Node changes | `Watches(&Node{})` with `NodePredicate` | Node added, removed, cordoned, uncordoned, or labels change |
| Pod changes | `Watches(&Pod{})` with `cacheserverPodPredicate` | Cache-server pod scheduling or status changes |
| Stabilization requeue | `RequeueAfter` on reconcile result | Timer expires after oldest window recommendation ages out |

### Node Predicate

Not every node event triggers reconciliation. `NodePredicate` filters to only
the events that could change the scaling decision:

- **Create:** always enqueued (new node may match selector).
- **Delete:** always enqueued (node removed from pool).
- **Update:** enqueued only when `spec.unschedulable` changes (cordon/uncordon)
  or node labels change. Other updates (status heartbeats, resource pressure
  conditions) are ignored.

The `MapNodeToCaches` function finds all Cache CRs whose
`spec.nodeSelectorKey/Value` matches the changed node's labels and enqueues
reconciliation for each.

### Reconciliation Flow

```
Reconcile(Cache)
  |
  +-- Static scaling?
  |     +-- Purge normalizer history (no stabilization needed)
  |     +-- Set status.scaling = nil
  |     +-- Return spec.static.numServers
  |
  +-- Dynamic scaling
        +-- List schedulable nodes matching nodeSelector
        +-- Calculate rawDesired (with server bounds)
        |
        +-- First reconciliation (currentReplicas == 0)?
        |     +-- Seed normalizer history (no stabilization applied)
        |     +-- Set status.scaling = {targetServers: N}
        |     +-- Return rawDesired (immediate bootstrap)
        |
        +-- Subsequent reconciliations
              +-- normalizer.NormalizeDesiredReplicas()
              |     +-- validateBehavior()
              |     +-- tolerance check
              |     +-- stabilization window
              |
              +-- RequeueAfter > 0?
              |     +-- YES: set status.scaling = {targetServers: N, stabilizationEnds: ...}
              |     |        log "stabilization holding back scaling"
              |     |        schedule requeue
              |     +-- NO:  set status.scaling = {targetServers: N}
              |
              +-- Return stabilized replica count
```

### RequeueAfter

Kubernetes controllers are event-driven -- they reconcile only when a watched
resource changes. But stabilization windows are inherently time-driven: a
recommendation that was blocked now may become actionable when an older
recommendation expires.

`RequeueAfter` bridges this gap. When the normalizer holds back a change, it
computes when the oldest recommendation in the active window will expire and
returns that duration:

```
expiry = oldestRecommendationTimestamp + window
requeueAfter = expiry - now
```

The controller passes this to the status helper, causing the workqueue to
re-enqueue the Cache CR after the specified delay.

Without `RequeueAfter`, a stabilization-blocked decision would remain stuck
until an unrelated event (like a node change) happened to trigger
reconciliation.

### Interaction Between Requeue and External Events

Multiple events can race with the requeue timer:

- **Node added/removed during window:** A node event triggers immediate
  reconciliation. The normalizer records a new recommendation and re-evaluates
  the window. If the new recommendation is more conservative than the held-back
  one, the window extends naturally.
- **User changes behavior during window:** If the user modifies the scaling
  spec, the normalizer detects the change, resets stabilization timestamps,
  and scales immediately on the next reconciliation -- the prior window is
  discarded.
- **Multiple requeue timers:** The controller framework deduplicates requeue
  requests -- if a reconciliation has already been scheduled sooner than the new
  `RequeueAfter`, the earlier one wins.

## State Management

### Per-Cache Normalizer State

Each Cache CR has independent normalizer state stored in a `sync.Map` keyed by
`namespace/name`. The state is two timestamps (`lastAtOrAbove`, `lastAtOrBelow`)
and a copy of the last-seen scaling spec -- O(1) memory regardless of how many
reconciliations occur.

State is purged:

- When a Cache switches from dynamic to static scaling.
- When a Cache CR is deleted (via the finalizer cleanup in `Deleted(key)`).

## Scaling Status

The controller exposes stabilization state through the `status.scaling` field
on the Cache CR (not as a condition, since stabilization is an internal
optimization and should not affect the aggregate `Ready` state):

```yaml
status:
  scaling:
    targetServers: 5
    stabilizationEnds: "2024-01-15T10:05:00Z"
    message: "Stabilization window active, scaling to 5 servers after 2024-01-15T10:05:00Z"
```

| Field | Description |
|-------|-------------|
| `targetServers` | The server count the scaling algorithm computed. Nil on error paths. When a stabilization window is active this is the count the system would apply once the window expires. |
| `stabilizationEnds` | Set when a stabilization window is holding back a scaling change. The time at which the current window expires. Nil when settled. |
| `message` | Human-readable summary of the current scaling state. Uses absolute timestamps so the value stays stable across reconcile loops. |

The field is `nil` only for static scaling (stabilization does not apply).
For error paths (node listing failures, calculation errors, missing config),
the field is set with a `message` describing the error; `targetServers` will
be zero and `stabilizationEnds` nil. A non-nil `stabilizationEnds` indicates
a stabilization window is active; when it is nil the system has either settled
at `targetServers` or encountered an error (check `message`). This is visible
in `kubectl get cache -o yaml` and gives operators immediate insight into
whether scaling is being delayed and when it will resume.

## Design Decisions

### Tolerance as Integer Percentage

The tolerance fields use `int32` representing a percentage (0--100). This
provides a simple, intuitive config (`scaleUpTolerancePercent: 10` means 10%).

### First-Reconciliation Bypass

When `currentReplicas == 0` (the Cache is first created or transitioning from
static to dynamic scaling), the normalizer is seeded but not applied. This
allows the initial scale-up to happen immediately without waiting for a
stabilization window to expire.

### O(1) State Instead of Recommendation Lists

Each Cache CR's stabilization state is two timestamps and a spec copy -- O(1)
memory regardless of reconciliation frequency. This avoids accumulating entries
proportional to the reconciliation frequency * window duration. The `sync.Map`
is ideal because different Cache CRs reconcile concurrently but access
different keys.

## Debugging

```bash
# Check scaling status
kubectl get cache <name> -n dacs-system -o jsonpath='{.status.scaling}' | jq .

# Check conditions
kubectl get cache <name> -n dacs-system -o jsonpath='{.status.conditions}' | jq .

# Watch for scaling events
kubectl describe cache <name> -n dacs-system | grep -A5 scaling

# Check current vs desired
kubectl get cache <name> -n dacs-system -o jsonpath='{.status.desiredServers} desired, {.status.currentServers} current'
```

The scaling controller logs at verbosity level 1 (`V(1)`):

- `"Scaling calculation"` - emitted on every dynamic reconciliation with
  `matchingNodes`, `percentage`, `rawDesired`, and `currentReplicas`.
- `"Stabilization holding back scaling"` - emitted when `RequeueAfter > 0`,
  includes `rawDesired`, `stabilized` (the held-back count), and `requeueAfter`.

Enable with `--zap-log-level=1` or equivalent controller-runtime log
configuration.

## Implementation Reference

| Component | File | Purpose |
|---|---|---|
| Scaling reconciler | `internal/scaling/scaling_controller.go` | Main entry point, node lifecycle integration |
| Calculator | `internal/scaling/calculator.go` | Raw desired-count formula |
| Normalizer | `internal/scaling/normalizer.go` | Tolerance + stabilization window |
| Node predicate | `internal/scaling/node_predicate.go` | Filters relevant node events |
| Config | `internal/config/config.go` | Default tolerance and stabilization values |
