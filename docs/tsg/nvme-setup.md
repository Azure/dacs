# DACS NVMe Prerequisite Setup Failure

## Symptoms

The prerequisite DaemonSet is missing from expected nodes, its init container
fails or restarts, or the expected cache mount is absent. Logs may report
package-installation failures or `No unused NVMe devices found.`.

## Description

The `dacs-prereq` chart prepares local NVMe storage on selected nodes. Single-disk
installations use ext4 directly; multiple disks use RAID0. The setup script can
exit successfully when no unused NVMe disks are found, so a Ready prerequisite
pod alone does not prove that the cache mount exists.

This guide does not apply to `cache.storage.ephemeral=true`; the ephemeral
quickstart does not require the prerequisite chart.

## Steps to Mitigate

1. Inspect the DaemonSet and affected pod. Replace `<prereq-pod>` with its actual
   name; these commands assume the default prerequisite name `dacs-prereq`:

   ```bash
   export NAMESPACE="dacs-system"
   kubectl get daemonset dacs-prereq -n "$NAMESPACE"
   kubectl get pods -n "$NAMESPACE" -l app.kubernetes.io/name=dacs-prereq -o wide
   kubectl describe pod <prereq-pod> -n "$NAMESPACE"
   kubectl logs <prereq-pod> -n "$NAMESPACE" -c nvme-raid-setup --tail=200
   ```

   If the init container restarted, also collect its `--previous` logs.
2. For zero desired pods or missing nodes, check prerequisite `nodeSelector` and
   `tolerations` against the intended cache nodes.
3. For admission denial of privileged containers or host PID access, ask the
   cluster administrator to review required privileges. Do not weaken
   cluster-wide policy as a workaround.
4. For package failures, verify host package-repository access. Supported host
   package managers are Azure Linux `tdnf` and Ubuntu `apt-get`.
5. For `OOMKilled`, inspect the deployed init-container limit. Current defaults
   request `512Mi` and allow `2Gi`; older or overridden limits may be too small
   for package metadata refreshes.
6. For no unused disks or a missing mount, confirm the VM has local NVMe data
   disks. With approved access, run these **read-only commands on the host**:

   ```bash
   lsblk -o NAME,TYPE,SIZE,FSTYPE,LABEL,MOUNTPOINT
   findmnt --mountpoint /var/lib/ssd/cacheserver
   cat /proc/mdstat
   df -h /var/lib/ssd/cacheserver
   ```

   Substitute your configured mount point. Inspect disk signatures and ownership;
   do not erase them to make setup proceed. A missing RAID array is not itself
   a failure for a single-disk installation.
7. Ensure prerequisite `mountPoint` matches
   `cache.storage.hostPath.nodeCacheFolderPath`.

NVMe setup can format unused disks. Confirm node and disk ownership before
installing or rerunning it. Do not run `mkfs`, `wipefs`, or force-create a RAID
array as a troubleshooting shortcut.

## Verification Steps

Confirm setup completes on every intended cache node. Use `findmnt` and `lsblk`
on each host to verify the configured path is a mount backed by the intended
data disk/filesystem, not merely a directory on the OS disk. Verify CacheServers
using that storage can start.

## Contact Information

Contact the cluster/storage administrator before disk or host changes.
For DACS help, use [SUPPORT.md](../../SUPPORT.md) and include redacted host
diagnostics from the [escalation checklist](../installation-troubleshooting.md#escalation-checklist).
