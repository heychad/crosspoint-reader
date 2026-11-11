---
name: firmware-handoff
description: Risk-based review and device handoff for a firmware candidate, release, or supported-branch merge. Not required for checked WIP checkpoints or read-only work.
---

# Firmware handoff

Use at a testable-device candidate, release, or supported-branch merge. Review
one cumulative candidate, not each feature separately. WIP checkpoints follow
AGENTS.md. The main agent owns implementation, checks, and the final verdict.

## Prepare the candidate

Record the requirement, comparison base, candidate revision, changed files,
SDK pin/patches, enabled build flags, checks, and unresolved findings. Account
for unrelated work. Reuse passing evidence while its inputs remain unchanged.
Use the existing formatter and X4 Pro toolchain; no other board is required.

## Select review depth

Choose from the actual changed contracts. Record the reason once.

| Candidate | Independent review |
| --- | --- |
| Documentation, formatting, or test-only change with no firmware effect | Main-agent review and relevant checks |
| Bounded UI, settings, shortcuts, or reuse of an existing reader action | One combined correctness/architecture review; include input and resource checks where affected |
| Persistent writes, migrations, deletion, concurrency, SDK patches, or ownership/resource changes | Correctness review plus a specialist review of the affected storage or embedded contract |
| Broad upstream integration, or a candidate combining substantial storage, resource, and UI changes | Four axes: correctness, architecture, embedded, and i18n/docs |

The existing `review-correctness`, `review-architecture`, `review-embedded`, and
`review-i18n-docs` skills provide the applicable criteria. One reviewer can cover
multiple axes on a bounded candidate. Do not spawn reviewers during routine WIP
implementation. A complex change cannot qualify as bounded just because it sits
behind a flag.

Give independent reviewers the requirement, pinned diff, constraints, and check
results without coaching their verdict. Reviewers inspect source, history, and
recorded results only: no writes, tests, builds, commits, or deployment. Reserve
a slot for the main agent. Freeze the candidate while reviews run.

## Resolve findings

Verify findings against reachable behavior. Combine duplicates, fix defects as
one batch, and defer optional cleanup outside the requirement. Seek targeted
follow-up only when a fix changes a reviewed contract; formatting and prose do
not restart review. Stop after two rounds. If material issues remain, isolate
or disable that feature and advance the rest of the candidate when its
independence is established. Keep unresolved issues visible; do not silently
call them fixed or restart an unlimited review loop.

Model failures appropriate to the change. State the supported corruption and
power-loss behavior explicitly; do not expand a local feature into a general
filesystem-repair project without evidence and a scope decision.

## Verify and hand off

Run the full host suite for the cumulative device candidate and one X4 Pro build
per intended configuration. Reuse checks/builds that already cover the final
inputs. Test flags independently when they change compiled behavior. Record
commands, results, and unavailable checks; unavailable is not passed.

Explain changed behavior, important tradeoffs, known limits, and rollback. Honor
Chad's standing acceptance of routine design and maintenance decisions; ask only
when a material choice falls outside it. Give a short device smoke test covering
the changed behavior, reading-position preservation, and affected input/layout
paths. Reserve exhaustive regression for releases or a demonstrated regression.

The human installs firmware and reports physical behavior. Never infer device
validation from compilation or file hashes. Publication and supported-branch
merges require their existing authorization. PR ownership remains in AGENTS.md.
