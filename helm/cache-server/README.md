# CacheServer Helm Chart

This chart deploys the CacheServer distributed caching system for Kubernetes.

## Components

- **CacheServer StatefulSet**: Distributed cache servers that store data on local SSD storage
- **CacheServer Scheduler**: Custom Kubernetes scheduler that provides cache-aware pod placement
- **Services**: Headless service for StatefulSet DNS and discovery service for load balancing

## Installation

```bash
helm install cache-server ./helm/cache-server -n cache-server --create-namespace
```

## Configuration

See `values.yaml` for all configuration options.

### Key Settings

| Parameter | Description | Default |
|-----------|-------------|---------|
| `cacheServer.enabled` | Enable CacheServer deployment | `true` |
| `cacheServer.numServers` | Number of cache server replicas | `3` |
| `cacheServer.serverSizeInGB` | Size of cache per server in GB | `4` |
| `cacheServer.scheduler.enabled` | Enable custom scheduler | `true` |
| `cacheServer.useHostPath` | Use hostPath storage instead of PV/PVC | `true` |
| `cacheServer.enableTrafficControl` | Enable traffic control for bandwidth limiting | `true` |
| `cacheServer.nodeCacheFolderPath` | Host path for cache storage | `/var/lib/ssd/cacheserver` |

## Architecture

The CacheServer uses a custom scheduler to ensure optimal pod placement across nodes. When `scheduler.enabled` is true:

1. The scheduler watches for pods with `schedulerName: cacheserver-scheduler`
2. It coordinates with existing cache servers to avoid placing multiple pods on the same node
3. Supports scheduler coordination via ConfigMap for multi-instance deployments
