# DACS Container Image Pull Failure

## Symptoms

A controller, CacheServer, prerequisite, or client pod reports `ErrImagePull`
or `ImagePullBackOff`.

## Description

The node cannot retrieve a required container image. Typical causes include an
incorrect repository/tag, registry connectivity or authentication failures, and
an image that does not support the node architecture.

## Steps to Mitigate

1. Set the namespace, identify the affected pod, and inspect its image and events.
   Replace `<pod-name>` with the affected pod:

   ```bash
   export NAMESPACE="dacs-system"
   kubectl get pods -n "$NAMESPACE" -o wide
   kubectl describe pod <pod-name> -n "$NAMESPACE"
   kubectl get pod <pod-name> -n "$NAMESPACE" \
     -o jsonpath='{range .spec.initContainers[*]}{.image}{"\n"}{end}{range .spec.containers[*]}{.image}{"\n"}{end}'
   ```

2. For `manifest unknown` or `not found`, correct the repository and image tag.
   Remove unintended overrides or align them with the installed chart's
   `appVersion`. Use the image path specified by the relevant installation
   procedure; do not assume every client uses the same repository path.
3. For DNS, connection, or TLS errors, restore node-side registry access,
   including any required proxy or trusted certificates. A workstation pull
   does not establish node connectivity.
4. For `unauthorized`, verify the registry and repository. Published artifacts
   at `dacstest.azurecr.io` allow anonymous pulls. For private mirrors, configure
   image-pull authentication using the supported deployment mechanism.
5. For platform mismatches, use a supported Linux node architecture (`amd64` or
   `arm64`) and an image that includes that platform.

## Verification Steps

Inspect the affected pod again. Confirm the image is pulled, the container
starts, and no new image-pull errors appear. For a client Job, confirm it
completes successfully rather than expecting its pod to remain Running.

## Contact Information

Contact the cluster or registry administrator for node connectivity and private
registry access. For DACS help, use [SUPPORT.md](../../SUPPORT.md) and the
[escalation checklist](../installation-troubleshooting.md#escalation-checklist).
