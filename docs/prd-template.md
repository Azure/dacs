# PRD Template: <Feature Name>

> Copy this file to `docs/prds/<short-feature-name>.md` and fill it in. See
> [`docs/prd-process.md`](prd-process.md) for when a PRD is needed, and how it is reviewed and
> approved.
>
> A PRD answers **Why** and **What** — the problem, customer challenge, goals, requirements, and
> a high-level proposal. It should **not** contain implementation details: architecture, API
> design, and other **How** decisions belong in the separate Design Proposal / Design Document
> process, after this PRD is approved.

## Status

| Field | Value |
|---|---|
| Author(s) | |
| Date | |
| Status | Draft / In Review / Approved / Rejected |
| Review deadline | |
| Maintaining team(s) | |

## Executive Summary

One paragraph: the problem, the proposed direction, and why now.

## Problem Statement

- Who is the customer (internal team, external customer, both)?
- What is the pain point?
- What is the current workaround, if any?
- Why is the existing solution (or lack of one) insufficient?

## Customer Scenarios / Personas

Describe the users/personas affected and representative scenarios or user stories.

## Goals

What must the solution achieve? List as concrete, verifiable goals. Use this section to keep
design discussions from expanding scope later.

## Non-Goals

What is explicitly out of scope for this PRD.

## Proposed Solution (High-Level)

A brief, high-level description of the direction — enough for reviewers to sanity-check
feasibility and scope, but not a design. Detailed design belongs in the Design Document.

## Success Metrics

Measurable outcomes and targets, not qualitative statements. Consider:

- Business outcomes
- Customer outcomes
- Technology outcomes (e.g. latency targets, cache hit-rate targets, throughput targets)
- Adoption targets

## Requirements

### Functional Requirements

What capabilities must the solution provide?

### Non-Functional Requirements

Performance, scalability, reliability, security, supportability, and operability requirements the
solution must satisfy.

### Test Requirements

What must be validated (and how) before this can ship?

## Dependencies

- Partner teams involved.
- Required commitments/deliverables from other teams.

## Ownership

Proposed ownership model for the resulting feature (who is expected to maintain it long term).
Note: this records intended ownership, not implementation priority — see
[`docs/prd-process.md`](prd-process.md).

## Risks / Open Questions

List unresolved questions or risks to be addressed during review or the subsequent design phase.

## FAQ

Answer the objections/questions reviewers are likely to raise (e.g. "why can't existing solution
X do this?", "why is this needed?", "what is the scope of the first release?").

## Approval

Per [`docs/prd-process.md`](prd-process.md), every team listed in `Maintaining team(s)` above
must provide one Engineering approver and one PM/Product approver. Add a row pair per maintaining
team.

| Team | Role | Name | Approved | Date |
|---|---|---|---|---|
| (all) | Feature Owner | | | |
| (all) | Independent reviewer | | | |
| <team A> | Engineering | | | |
| <team A> | PM / Product | | | |
| <team B> | Engineering | | | |
| <team B> | PM / Product | | | |
