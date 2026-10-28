# DACS Admission Webhook Failure

## Symptoms

Cache creation or updates fail with `failed calling webhook`,
`no endpoints available`, connection refused, a timeout, or an `x509` error.
Alternatively, the webhook rejects an invalid Cache field.

## Description

The DACS admission webhook defaults and validates Cache resources. The Kubernetes
API server must reach its Service and trust its serving certificate. By default,
the controller manages webhook certificates; cert-manager is not required for
the default installation. A field-validation rejection is not a connectivity
failure.

## Steps to Mitigate

1. Inspect the Service, endpoints, and controller logs. These names assume
   `namePrefix=dacs`; substitute your actual names if different:

   ```bash
   export NAMESPACE="dacs-system"
   kubectl get service dacs-webhook-service -n "$NAMESPACE"
   kubectl get endpointslices -n "$NAMESPACE" \
     -l kubernetes.io/service-name=dacs-webhook-service
   kubectl logs deployment/dacs-manager -n "$NAMESPACE" -c manager --tail=200
   ```

2. For no ready endpoints, restore
   [controller readiness](controller-startup.md).
3. For timeouts, verify API-server connectivity to the webhook Service, including
   cluster firewall and network policies. The default Service exposes TCP `443`
   and targets controller port `9443`.
4. For certificate errors, inspect certificate-management logs and confirm that
   the webhook configuration references the intended Service and namespace.
   Do not print TLS private keys or delete certificate Secrets blindly.
5. If optional integrations were enabled, verify their prerequisites separately.
6. For validation rejection, correct the Cache field named in the error.
   Do not disable admission validation or change the failure policy merely to
   bypass the failure.

## Verification Steps

Confirm the webhook Service has ready endpoints. Retry the original Cache
creation or update and confirm it is accepted without connection or certificate
errors. For invalid input, verify the corrected resource passes validation.

## Contact Information

Contact the cluster administrator for API-server connectivity or policy changes.
For DACS help, use [SUPPORT.md](../../SUPPORT.md) and the
[escalation checklist](../installation-troubleshooting.md#escalation-checklist).
