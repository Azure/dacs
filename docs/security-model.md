# Security model and current posture

> [!IMPORTANT]
> DACS is pre-release software and its security hardening is ongoing. This
> document describes the security model DACS assumes **today**. Read it before
> deploying DACS, and re-check it as the project evolves.

## Summary

DACS is currently designed for deployments in which **the trust domain is the
entire Kubernetes cluster**. Every component and every workload that can reach a
DACS endpoint is assumed to be mutually trusted. DACS does **not yet provide
tenant isolation and is not suitable for multi-tenant clusters** or any
environment where the cache is expected to enforce a security boundary between
workloads.

If you cannot treat every pod that can reach DACS — and everyone who can create
or modify DACS resources — as fully trusted, DACS is not ready for your
environment in its current form.

## Intended trust model

DACS assumes a **single trust domain equal to the whole cluster**:

- **Data plane.** Any workload that can open a network (TCP or RDMA) connection
  to a CacheServer is treated as authorized to perform cache operations —
  reading, writing, deleting, locking, and managing attributes of cached data.
  Reachability is effectively authorization in this release.
- **Cached data.** All data in the cache should be considered readable and
  writable by any workload that can reach a CacheServer. Do not place data in
  the cache that must be kept confidential from other workloads in the cluster.
- **Management plane.** The cache controller, scheduler, and admission webhook
  are cluster-privileged components. Anyone able to create or modify a `Cache`
  custom resource can influence where and how cache workloads are scheduled.
  Treat `Cache` CRD access as a privileged operation.

> [!NOTE]
> CacheClient offers an optional, opt-in client-side encryption feature: callers
> may supply a per-call encryption key and CacheClient will encrypt payload data
> (AES-GCM) before upload and decrypt it after download, so the CacheServer never
> observes plaintext data. This is a data-confidentiality convenience for callers
> who manage their own keys, **not** a supported security boundary — key
> management, rotation, and distribution are entirely the caller's
> responsibility, and the feature is under active review (see
> [What DACS does not provide yet](#what-dacs-does-not-provide-yet)).

## What DACS does not provide yet

The current release does **not** provide, and you should **not** rely on:

- **Tenant isolation** between workloads sharing a cluster.
- **Authentication or authorization** of data-plane clients as an enabled,
  default security boundary.
- **Transport encryption (TLS/mTLS)** of the data plane by default.
- **Confidentiality or integrity guarantees** from the optional
  encryption/checksum features. These exist but are under active review and must
  not be treated as a security boundary in this release.

Because of the trust model above, these are **by-design assumptions for a
single-trust-domain deployment**, not defects. They do, however, make DACS
unsuitable for multi-tenant or zero-trust use until the roadmap items below
land.

## Recommended deployment guidance

Until DACS provides tenant isolation, operators should enforce the trust
boundary at the infrastructure layer:

- **Dedicate the cluster.** Run DACS in a cluster where only trusted workloads
  are scheduled. Avoid shared clusters that host untrusted or mutually
  distrusting tenants.
- **Restrict network reach.** Use Kubernetes `NetworkPolicy` (or equivalent) to
  allow only trusted client pods to reach CacheServer endpoints, and do not
  expose cache endpoints outside the cluster.
- **Restrict `Cache` CRD access.** Limit, via RBAC, who can create or modify
  `Cache` resources to trusted cluster operators.
- **Install from the provided manifests.** Deploy the controller, scheduler, and
  webhook using the charts in this repository so their RBAC and pod settings
  match what the project has reviewed.
- **Keep sensitive data out of the cache.** Do not cache secrets or data that
  must remain confidential from other in-cluster workloads.

## Roadmap toward broader deployment models

Work to make DACS suitable for stronger trust models — including multi-tenant
clusters — is in progress. Planned directions include:

- Secure-by-default configuration for the data plane.
- Authentication and transport encryption (e.g., mTLS) for cache traffic.
- Tenant-scoped authorization of cache operations.
- Least-privilege RBAC and scheduling for the management components.
- Hardened pod security defaults.

This document will be updated as these capabilities land and as the assumed
trust model narrows.

## Reporting a vulnerability

Please do **not** open public GitHub issues for security vulnerabilities. Use
the private reporting process described in [SECURITY.md](../SECURITY.md).
