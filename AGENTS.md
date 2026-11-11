# CrossPoint Reader agent guide

This personal CrossPoint fork targets the Xteink X4 Pro only. Build, review and
validate the X4 Pro profile; other inherited board profiles are not supported
release targets. Keep the next hardware test small, preserve reading state and
use its results to guide the next change.

## FreeInk SDK

The [FreeInk SDK](freeink-sdk/) supplies the board profiles, hardware drivers,
and shared UI components. Before adding an API or device-specific code, check
the firmware HAL and the SDK source at the revision pinned by this repository.
Use the [SDK documentation](freeink-sdk/docs/README.md) and
[FreeInk SDK index](https://freeink.org/llms.txt) to find the relevant contract.
Check `git submodule status`: a different local SDK revision must be accounted
for, not silently treated as the pinned firmware dependency.

## Start here

At session start, run `uname -s`, `git branch --show-current`, `git remote -v`, and `git status --short`. Use `codex/` branches for checked development work. `develop` is the supported integration target; never infer permission to merge into it.

Act as a senior embedded C++ engineer. Base claims on repository evidence: cite the paths and line numbers that justify a proposed change. Explain the mechanism behind performance or memory claims and justify every new heap allocation. For every fix, tell the human how to verify it.

First inspect the relevant code and current behavior to establish whether a change is needed. Before adding an API, setting, task, persistent state, driver change, or scheduling dependency, tie it to the human requirement and check existing mechanisms. Identify unexplained scope inherited from a stash or branch.

Use the table to find guidance for the decisions being changed. Read relevant sections, not every file associated with a touched subsystem. Load implementation skills when implementation is needed and `firmware-handoff` at final handoff. Reuse guidance already in context unless it changed or a concrete question requires another read.

| Decision being changed | Relevant guidance |
| --- | --- |
| Routine builds or host setup | [existing environment](.agents/rules/environment.md#build-in-an-existing-environment) |
| Personal build flags or ports | [local overrides](.agents/rules/environment.md#platformiolocalini-personal-overrides) |
| Allocation size/failure, buffer or cache lifetime, hot-path containers, or hardware limits | [hardware-resources.md](.agents/rules/hardware-resources.md); use `heap-discipline` for allocation decisions |
| Hardware/storage access, rendering contracts, input ownership, or SDK boundaries | [HAL contracts](.agents/rules/architecture-hal.md#hardware-abstraction-layer-hal) and relevant sections of `hal-and-abstractions` |
| Build flags, dependencies, or board profiles | [build environment and flags](.agents/rules/architecture-hal.md#build-environment) |
| C or C++ implementation | [coding conventions](.agents/rules/coding-standards.md#naming-conventions); consult the relevant pitfall/allocation section and use `control-flow-clarity` for state modeling |
| Lists, controls, touch routing, or layout | [shared UI](.agents/rules/ui-activities.md#shared-ui-and-input) and [orientation](.agents/rules/ui-activities.md#orientation-aware-logic) |
| Input edges or activity/popup transitions | [input ownership](.agents/rules/ui-activities.md#input-frames-and-ownership) and its transition/verification sections |
| Activity resources or worker tasks | [activity lifecycle](.agents/rules/ui-activities.md#activity-lifecycle-and-memory-management) and [workers](.agents/rules/ui-activities.md#freertos-task-guidelines) |
| Runtime font discovery, rendering, or cache lifetime | [font ownership](.agents/rules/ui-activities.md#runtime-fonts-and-cache-ownership) |
| Plugin, service, web endpoint, or protected-book contracts | [service contracts](.agents/rules/architecture-hal.md#service-and-plugin-contracts) and the linked contract that applies |
| Builds, formatting, or verification | [testing workflow](.agents/rules/testing-debugging.md#testing-and-verification-workflow) and [compact build output](.agents/rules/testing-debugging.md#compact-build-output) |
| Crashes, CI, or serial measurements | [crash diagnosis](.agents/rules/testing-debugging.md#debugging-crashes) or the matching CI/serial section |
| Branch creation, merges, commits, or publication | [git-workflow.md](.agents/rules/git-workflow.md) |
| User-facing strings or language metadata | [translation workflow](.agents/rules/generated-files-cache.md#translation-workflow) |
| Generated HTML/i18n or font assets | [generated sources](.agents/rules/generated-files-cache.md#files-generated-by-build-scripts) or [font generation](.agents/rules/generated-files-cache.md#font-generation) |
| Cache formats, layout persistence, or invalidation | [versioning](.agents/rules/generated-files-cache.md#cache-file-format-versioning) and [invalidation](.agents/rules/generated-files-cache.md#cache-invalidation-rules) |
| New features, activities, settings, libraries, or dependencies | `SCOPE.md` and the `scope-discipline` skill |
| Restructuring, extraction, or separating unrelated cleanup | the `refactor-for-review` skill |

Repository-local skills live under `.agents/skills/`. Their descriptions identify specific procedures to use when needed; a keyword match alone does not require loading a skill.

## Human ownership

A PR is a long-term maintenance commitment. Working code is not enough: prefer the simplest design that meets the real requirement, fits `SCOPE.md`, and can be understood and maintained by its human owner.

The main agent may implement and checkpoint work within the human-authorized scope.
Review subagents inspect code,
diffs, history, and recorded checks only. The main agent owns verification:
reviewers may not write files (including temporary diagnostics), run tests/builds,
commit, publish, deploy, or flash.

The human must write PR descriptions; agents may give concise factual notes and test
results, never ready-to-paste PR prose. Creating/amending local commits requires
explicit human approval. Push only on an explicit human instruction to push;
edit/commit approval does not authorize it. Never open or close a PR.

Repository-facing prose: use plain English for non-native readers and standard
technical terms when clearest. Code comments must be short and useful after merge; follow the
[comment rules](.agents/rules/coding-standards.md#comment-style).

## WIP checkpoints

With the human's authorization, checkpoint unfinished work on a personal
development branch after reviewing the scoped diff, scanning for secrets and
unintended files, and running relevant checks. Label the commit WIP and record
experimental features, known failures, and unverified behavior. A WIP checkpoint
does not claim release readiness. Push only when the human has authorized it.
Keep generated build outputs and unrelated work out of the commit.

## Development cadence

Group related features that share a code path into a coherent batch, followed by
a checked WIP checkpoint. Prefer reusing upstream behavior over expanding a
custom implementation. Carry forward test and review evidence until relevant
code changes invalidate it.
Do not request repeated commit/push approval when the human has already
explicitly authorized recurring checkpoints for the current task. Keep release,
merge-to-supported-branch and physical-device authorization separate.

Compare stable and tagged release-candidate versions of CrossPoint and
CrossInk at integration milestones. Do not follow moving development branches.
Prefer an upstream implementation over a duplicate port. Follow the dated comparison
in `docs/downstream/UPSTREAM_STRATEGY.md`.

[Development process](docs/downstream/DEVELOPMENT_PROCESS.md) defines batch size,
verification triggers, and the boundary between implementation and release work.

## Mandatory firmware handoff

For every logical change that can affect shipped firmware or its build—including C/C++, build configuration, partitions, code-generation sources, translations, and release scripts—the main agent must complete [firmware-handoff](.agents/skills/firmware-handoff/SKILL.md) before declaring release readiness, releasing firmware, or merging to the supported branch. Authorized WIP checkpoints follow the lighter process above. The handoff skill selects review depth from the actual risk, reuses valid evidence, and respects standing human delegation. It does not require four reviews for every change.

Read-only inquiries, audits, and reviewers do not run implementation handoff. Pure tests, diagnostics, documentation, and host-only Python scripts use a lighter review unless they alter firmware output or its build. Hardware testing remains the human's responsibility and is required before a PR is opened.
