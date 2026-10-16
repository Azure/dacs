# Certificate Management for Webhooks

## Overview

Kubernetes admission webhooks require TLS certificates for secure communication with the API server. The cache-controller uses the **OPA cert-controller** library to automatically generate and rotate these certificates at runtime.

**Library:** [open-policy-agent/cert-controller](https://github.com/open-policy-agent/cert-controller)

## Components

### 1. Cert Rotator (in controller process)

The cert-controller `rotator` package is imported as a Go library and runs inside the cache-controller process. It's configured in `cmd/main.go` and starts automatically with the controller manager.

### 2. Certificate Secret

- **Name:** `cache-controller-webhook-server-cert`
- **Namespace:** `cache-controller-system`
- **Contents:** `tls.crt`, `tls.key`, `ca.crt`
- **Purpose:** Persists certificates across controller restarts

The rotator creates this secret if it doesn't exist, or updates it when certificates are rotated.

### 3. Webhook Configurations

The rotator automatically injects the CA bundle into:
- `MutatingWebhookConfiguration` - for the defaulting webhook
- `ValidatingWebhookConfiguration` - for the validation webhook

This allows the Kubernetes API server to trust the webhook's TLS certificate.

### 4. Certificate Directory

- **Path:** `/tmp/k8s-webhook-server/serving-certs`
- **Purpose:** The webhook server reads certificates from this directory

Kubelet projects the certificate Secret into this directory so the
controller-runtime webhook server can use it. Secret updates are asynchronous;
generating certificates does not make the mounted files available immediately.

## How It Works

1. Controller starts -> cert rotator initializes
2. Rotator generates self-signed CA + server certificate
3. Certificates stored in Secret and projected into the pod by kubelet
4. CA bundle injected into webhook configurations
5. Webhooks registered (only after certs are ready)
6. Rotator monitors expiry and auto-renews

## Health Probes

`/healthz` checks process liveness independently of certificate initialization.
`/readyz` succeeds only after certificate setup completes and the webhook server
starts. With webhooks disabled, readiness does not depend on certificates.

The Helm chart's startup probe checks `/readyz` every five seconds and allows
60 failures, giving initialization approximately five minutes before Kubernetes
restarts the container. Liveness and readiness probes begin after startup
succeeds. Keep `EnableReadinessCheck` enabled in the cert rotator so webhook
configuration reconciliation waits for mounted certificates.

Unit tests cover the registered liveness and readiness checks, certificate
initialization, and webhook startup. Run them from
`src/subsystems/cache-controller`:

```bash
CACHE_CONTROLLER_CONFIG_PATH="$PWD/internal/config/cache-controller-config.yaml" \
  go test ./cmd
```

## RBAC Requirements

| File | Purpose |
|------|---------|
| `config/rbac/certificate_role.yaml` | Read/write Secret in controller namespace |
| `config/rbac/webhook_role.yaml` | Update MutatingWebhookConfiguration and ValidatingWebhookConfiguration |

## Troubleshooting

Check certificate secret:
```
kubectl get secret cache-controller-webhook-server-cert -n cache-controller-system
```
