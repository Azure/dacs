# DACS Product Requirements Document (PRD) Process

This is a lightweight PRD process for DACS. The key idea: a PRD answers **Why** and **What**; a
separate Design Doc answers **How**. PRDs should not contain implementation details.

## Feature Lifecycle

The PRD is the first formal gate for any significant feature:

1. Identify customer problem / opportunity.
2. **Write and approve PRD.**
3. Design Proposal (optional, for exploring options before committing to a design).
4. Design Document (required for anything that changed behavior significantly, before
   implementation starts).
5. Implementation, testing, and release.

A PRD unblocks design work; it does not itself authorize implementation priority (see
[Ownership and Prioritisation](#ownership-and-prioritisation)).

## When a PRD is Required

A PRD should be created for:

- New DACS core features.
- Significant API changes.
- Features requiring cross-team ownership or maintenance.
- Changes with long-term supportability, performance, or architectural implications.

Minor bug fixes and routine maintenance do not require a PRD.

## How to Create a PRD

1. Copy [`docs/prd-template.md`](prd-template.md) to `docs/prds/<short-feature-name>.md`.
2. Fill it in, focusing on the problem and requirements (Why/What), not solution design (How).
3. Open a pull request adding the file.
4. Use the PR description to link any related issues, and request review from the relevant DACS
   maintainers.
5. Discussion and approval happen via normal PR review (comments and approvals) — see below.

## Review Process: Async-First

Reviews are async-first. Use the PR comment thread as the primary review mechanism; only fall
back to a synchronous review meeting for unresolved questions or decisions that are not
converging async. This scales better than recurring review meetings for a distributed team.

## Review Deadline

When a PRD is opened for review, the author sets a review deadline (recommended: 5 business days)
and notifies the required approvers.

- Silence is **not** treated as approval — every quorum member listed below must give an explicit
  approval on the PR.
- If a required approver has not responded by the deadline, the author sends a reminder and, if
  still unresponsive, escalates to that approver's manager/delegate to name a substitute reviewer.
- The deadline exists to prevent PRDs from stalling indefinitely, not to bypass the quorum.

## Approval Quorum

A PRD identifies its **maintaining team(s)** (recorded in the PRD's `Maintaining team(s)` field)
— i.e. every team that owns and maintains code, an API, or an operational surface the feature
touches. This excludes teams that merely consume or are affected by the feature as end users.

Before implementation (or design) work begins, the PRD must receive explicit approval from:

- Feature Owner
- One independent reviewer (someone not otherwise involved in authoring the PRD)
- From **each** maintaining team:
  - One Engineering approver
  - One PM / Product approver

Any major objections should be resolved, escalated, or explicitly documented in the PR discussion
before work starts.

## Ownership and Prioritisation

- PRD approval establishes agreement that the problem is worth solving and records the intended
  ownership model.
- Approval does not guarantee immediate implementation priority.
- Prioritisation occurs during normal planning cycles, based on business impact and resource
  availability.

## After PRD Approval

Once a PRD is approved, solution design (architecture, API/data design, alternatives considered,
etc.) is handled through the separate Design Proposal / Design Document process, before
implementation begins.

## What We Deliberately Left Out

Intentionally not adopted, as they add overhead disproportionate to DACS's size:

- Marketing announcement / marketing deck requirements.
- Naming board approval.
- Large approval matrices beyond the quorum above.
- Launch Review Board mechanics.

## Trial

Distributed Pulls is the initial candidate feature used to trial this process, and the process
will be refined based on that experience before being applied more broadly.
