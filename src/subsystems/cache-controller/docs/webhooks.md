# Cache CRD Admission Webhooks

## Overview

The cache-controller uses Kubernetes [admission webhooks](https://kubernetes.io/docs/reference/access-authn-authz/extensible-admission-controllers/) to provide defaulting and validation for Cache custom resources. This ensures that:

1. **Invalid configurations are rejected immediately** - Users get clear error messages at `kubectl apply` time, not during reconciliation
2. **Defaults are applied consistently** - The controller can assume required fields are always set
3. **Validation logic is centralized** - All rules are in one place, not scattered across the codebase

## Architecture

```
┌─────────────────┐     ┌──────────────────────┐     ┌─────────────────┐
│  kubectl apply  │────▶│  Kubernetes API      │────▶│  etcd           │
│  Cache CR       │     │  Server              │     │  (storage)      │
└─────────────────┘     └──────────────────────┘     └─────────────────┘
                               │
                               │ 1. Mutating Webhook
                               ▼
                        ┌──────────────────────┐
                        │  /mutate-storage-    │
                        │  azure-com-v1-cache  │
                        │  (apply defaults)    │
                        └──────────────────────┘
                               │
                               │ 2. Validating Webhook
                               ▼
                        ┌──────────────────────┐
                        │  /validate-storage-  │
                        │  azure-com-v1-cache  │
                        │  (reject if invalid) │
                        └──────────────────────┘
```

## Mutating Webhook (Defaulter)

**Path:** `/mutate-storage-azure-com-v1-cache`

The mutating webhook applies default values to Cache resources when they are created or updated. This runs **before** validation.

### Default Values Applied

| Field | Default Value | Description |
|-------|---------------|-------------|
| `spec.port` | `9065` | Cache server listening port |
| `spec.metricsPort` | `9096` | Prometheus metrics port |
| `spec.nodeSelectorKey` | `dacs.azure.com/processing-unit` | Node label key for scheduling |
| `spec.nodeSelectorValue` | `gpu` | Node label value for scheduling |
| `spec.infinibandResourceLimitName` | `rdma/hca_rdma_infiniband` | Infiniband resource name |
| `spec.infinibandResourceLimitCount` | `0` | Infiniband device count |
| `spec.scaling.static.numServers` | `1` | Default server count (when no scaling config) |
| `spec.storage.ephemeral` | `false` | Use persistent storage by default |
| `spec.storage.hostPath.nodeCacheFolderPath` | `/var/lib/ssd/cacheserver` | Host path for cache data |
| `spec.eviction.enableAccessTimeEviction` | `true` | Enable LRU eviction |
| `spec.eviction.evictionMaintainFreeSpace` | `true` | Maintain free space buffer |
| `spec.eviction.lastAccessThreshold` | `30d` | Evict files not accessed in 30 days |
| `spec.eviction.sizeThreshold` | `90` | Start eviction at 90% full |
| `spec.eviction.sizeTarget` | `80` | Evict down to 80% full |
| `spec.eviction.passFrequency` | `1h` | Run eviction check hourly |

## Validating Webhook (Validator)

**Path:** `/validate-storage-azure-com-v1-cache`

The validating webhook rejects invalid Cache configurations with clear error messages. This runs **after** the mutating webhook.

### Validation Rules

#### Name Validation
| Rule | Error Message |
|------|---------------|
| Either `metadata.name` or `metadata.generateName` must be set | "metadata.name or metadata.generateName must be set" |
| `metadata.name` max 53 characters | "cache name must be at most 53 characters..." |
| `metadata.name` must be a valid RFC 1123 DNS label (lowercase alphanumeric or hyphens, must start/end with alphanumeric) | error details from `validation.IsDNS1123Label` |
| `metadata.generateName` max 48 characters | "cache generateName must be at most 48 characters..." |
| `metadata.generateName` must be a valid DNS-1123 prefix (the controller appends a 5-character random suffix; trailing hyphens are allowed) | error details from `validation.IsDNS1123Label` on `generateName + "aaaaa"` |

The 53-character limit ensures all derived resource names (longest suffix is `-discovery` at 10 characters) stay within the Kubernetes 63-character DNS label limit. When `generateName` is used, the apiserver appends a 5-character `[a-z0-9]` random suffix before the validating webhook runs, so the prefix limit is 48 characters (53 - 5).

#### Required Fields
| Field | Rule | Error Message |
|-------|------|---------------|
| `spec.serverSizeInGB` | >= 1 | "must be at least 1" |
| `spec.serverThreads` | >= 4 | "must be at least 4" |

#### Resource Limits
| Field | Rule | Error Message |
|-------|------|---------------|
| `spec.cpuLimit` | >= 0 (0 disables CPU requests and limits) | "must be at least 0" |
| `spec.memoryLimitMB` | 0 or >= 1024 (0 disables memory requests and limits) | "must be 0 or at least 1024" |
| `spec.egressBandwidthLimitGbps` | >= 1 (if set) | "must be at least 1" |
| `spec.infinibandResourceLimitCount` | >= 0 | "must be at least 0" |

When `spec.egressBandwidthLimitGbps` is set, the controller configures Linux
traffic control (`tc`) in the CacheServer container and grants the container
the `NET_ADMIN` capability. Leave this field unset when bandwidth limiting is
not required. `NET_ADMIN` expands the container's networking privileges, so
production deployments should grant it only after reviewing the workload
threat model and cluster network isolation.

#### Infiniband
| Rule | Error Message |
|------|---------------|
| If `spec.infinibandResourceLimitCount` > 0, `spec.infinibandResourceLimitName` is required | "required when infinibandResourceLimitCount > 0" |

#### Ports
| Rule | Error Message |
|------|---------------|
| `spec.port` must be 1-65535 | "must be between 1 and 65535" |
| `spec.metricsPort` must be 1-65535 | "must be between 1 and 65535" |
| `spec.port` != `spec.metricsPort` | "metricsPort must be different from port" |

#### Scaling Configuration
| Rule | Error Message |
|------|---------------|
| Must have either static OR dynamic scaling | "must specify either static or dynamic scaling" |
| Cannot have both static AND dynamic | "cannot specify both static and dynamic scaling" |
| `spec.scaling.static.numServers` 1-499 | "must be between 1 and 499" |
| `spec.scaling.dynamic.percentageOfNodesToUse` 1-100 | "must be between 1 and 100" |
| `spec.scaling.dynamic.minServers` 1-499 | "must be between 1 and 499" |
| `spec.scaling.dynamic.maxServers` 1-499 | "must be between 1 and 499" |
| `spec.scaling.dynamic.minServers` <= `spec.scaling.dynamic.maxServers` | "minServers cannot exceed maxServers" |

#### Storage Configuration
| Rule | Error Message |
|------|---------------|
| Cannot have both `spec.storage.ephemeral` AND `spec.storage.hostPath` | "cannot specify both ephemeral and hostPath storage" |
| If `spec.storage.hostPath`, `spec.storage.hostPath.nodeCacheFolderPath` required | "nodeCacheFolderPath is required" |

#### Eviction Configuration
| Rule | Error Message |
|------|---------------|
| Duration format (e.g., `30d`, `1h30m`) | "invalid duration format" |
| Percentage 0-100 | "must be between 0 and 100" |
| `sizeTarget` < `sizeThreshold` | "sizeTarget must be less than sizeThreshold" |

### Example Error

```bash
$ kubectl apply -f cache.yaml
Error from server (Invalid): error when creating "cache.yaml": admission webhook 
"validate.cache.storage.azure.com" denied the request: 
[spec.eviction.sizeTarget: Invalid value: "95": sizeTarget (95%) must be less than sizeThreshold (90%)]
```

## Implementation

### Files

| File | Purpose |
|------|---------|
| `api/v1/cache_webhook.go` | Webhook implementation |
| `api/v1/cache_webhook_test.go` | Unit tests |
| `api/v1/defaults.go` | Default value constants |
| `config/webhook/manifests.yaml` | Generated webhook configurations |

### Webhook Markers

The webhook paths are generated from kubebuilder markers:

```go
// +kubebuilder:webhook:path=/mutate-storage-azure-com-v1-cache,mutating=true,failurePolicy=fail,sideEffects=None,groups=storage.azure.com,resources=caches,verbs=create;update,versions=v1,name=mutate.cache.storage.azure.com,admissionReviewVersions=v1

// +kubebuilder:webhook:path=/validate-storage-azure-com-v1-cache,mutating=false,failurePolicy=fail,sideEffects=None,groups=storage.azure.com,resources=caches,verbs=create;update,versions=v1,name=validate.cache.storage.azure.com,admissionReviewVersions=v1
```

## Deployment Requirements

1. **cert-controller** - The integrated OPA cert-controller library provides TLS certificates for webhook endpoints (see [cert-controller.md](cert-controller.md))
2. **Webhook RBAC** - Controller needs permission to read/update webhook configurations
3. **Network access** - API server must be able to reach webhook endpoints

## Troubleshooting

### Webhook not being called

```bash
# Check webhook configuration
kubectl get mutatingwebhookconfigurations
kubectl get validatingwebhookconfigurations

# Check if CA bundle is injected
kubectl get mutatingwebhookconfigurations cache-controller-mutating-webhook-configuration -o yaml | grep caBundle
```

### Webhook returning errors

```bash
# Check controller logs
kubectl logs -n cache-system deploy/cache-controller-controller-manager -c manager

# Look for webhook-related errors
kubectl logs -n cache-system deploy/cache-controller-controller-manager -c manager | grep -i webhook
```
