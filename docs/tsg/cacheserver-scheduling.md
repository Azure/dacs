# DACS CacheServer Scheduling Failure

## Symptoms

CacheServer pods remain `Pending` and are not assigned to suitable nodes.
Events or controller logs may report selector mismatches, insufficient
resources, taints, or placement failures.

## Description

CacheServer placement depends on node eligibility, available resources, and
storage configuration. NVMe deployments default to nodes labeled
`dacs.azure.com/processing-unit=cpu`; the ephemeral quickstart selects
`kubernetes.io/os=linux`. Controller-managed placement may not produce
default-scheduler diagnostics.

## Steps to Mitigate

1. Inspect the affected pod, candidate node, assigned scheduler, and controller
   logs. Replace placeholders with actual names. The Deployment name assumes
   `namePrefix=dacs`:

   ```bash
   export NAMESPACE="dacs-system"
   kubectl describe pod <cache-pod> -n "$NAMESPACE"
   kubectl get nodes -L dacs.azure.com/processing-unit,kubernetes.io/os,kubernetes.io/arch
   kubectl get pod <cache-pod> -n "$NAMESPACE" \
     -o jsonpath='{.spec.schedulerName}{"\n"}{.spec.nodeSelector}{"\n"}'
   kubectl describe node <candidate-node>
   kubectl logs deployment/dacs-manager -n "$NAMESPACE" -c manager --tail=200
   ```

2. For selector mismatches, label only verified cache-capable worker nodes or
   correct `cache.nodeSelectorKey` / `cache.nodeSelectorValue` through the
   deployment configuration.
3. For insufficient CPU/memory, node pressure, or taints, add suitable capacity
   or correct the deployment configuration. Do not remove protective taints
   blindly.
4. Inspect controller logs as well as pod events for placement failures.
5. For host storage, confirm the prerequisite DaemonSet ran on the same eligible
   nodes and the expected mount exists. Follow the [NVMe setup TSG](nvme-setup.md).

## Verification Steps

```bash
kubectl get pods -n "$NAMESPACE" -l dacs.azure.com/cache-name=cache-sample -o wide
```

Substitute your actual Cache name. Confirm every requested cache pod is assigned
to an eligible node and progresses to Running. If a scheduled pod fails startup,
continue with the [CacheServer startup TSG](cacheserver-startup.md).

## Contact Information

Contact the cluster administrator for node capacity or policy changes.
For DACS help, use [SUPPORT.md](../../SUPPORT.md) and the
[escalation checklist](../installation-troubleshooting.md#escalation-checklist).
