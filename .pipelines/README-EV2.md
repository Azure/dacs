# EV2 Production Image Publisher

## Overview

Promotes Docker images and Helm charts from Corp ACR (`dacs`) to AME Production ACR (`dacsprodofficialacr`) using EV2's JIT approval system.

## Architecture

```
Corp Pipeline (53586)           EV2 Pipeline                 AME Production
┌─────────────────┐            ┌──────────────┐            ┌────────────────┐
│  Build Images   │            │ Pull Images  │  EV2 JIT   │  Production    │
│       ↓         │   Image    │      ↓       │  Approval  │  ACR           │
│  Push to Corp   │──  Tag  ──→│  Re-tag      │─────────→  │  dacsprod   │
│  ACR            │            │      ↓       │            │  officialacr   │
│                 │            │ Push to AME  │            │                │
└─────────────────┘            └──────────────┘            └────────────────┘
```

## Usage

1. **Corp pipeline** (53586) builds and publishes to `dacs` with tag `<buildTag>`
2. **Trigger EV2 pipeline** manually with parameter `SourceImageTag: "<buildTag>"`
3. **EV2 approval** grants JIT access, then pipeline promotes artifacts to production

## What Gets Promoted

**Docker Images** (amd64 + arm64):
- `cache-server`, `cache-test-client`, `cacheserver-scheduler`

**Helm Charts** (OCI):
- `dacs-prereq`, `cache-server`, `dacs`

## Required Permissions

Service principal (`azdacspool-mi`) needs:

**Source ACR (`dacs`):**
- `AcrPull` - Read artifacts

**Target ACR (`dacsprodofficialacr`):**
- `Container Registry Data Importer and Data Reader` - Import artifacts
- `AcrPush` - Write artifacts

## Why EV2?

Corp pipelines cannot directly access AME resources due to security boundaries. EV2 provides JIT approval, audit trails, and controlled production promotion.

## TODO

- [ ] Configure EV2 approval step for AME access to `dacsprodofficialacr`
- [ ] Grant service principal required permissions on production ACR
