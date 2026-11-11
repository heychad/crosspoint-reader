## Git Workflow and Repository Awareness

### Repository Detection Protocol

Use the session's branch, remote, and status checks; inspect their roles rather
than assuming `origin` is a fork or that an `upstream` remote exists.

### Git Operation Rules

- Integration and PR comparisons target `develop`, not `master` or remote HEAD.
- Authorization follows [human ownership](../../AGENTS.md#human-ownership).
  Before an explicitly requested push, recheck remotes and use the destination
  specified by the human. Otherwise prefer the personal-fork remote (`fork`
  when present); verify its URL rather than assuming a name establishes its role.
  Ask if the destination is unclear.
- No assistant co-author or generated-by trailers.
- When adapting/superseding another person's PR, verify the original human
  author and credit them with `Co-Authored-By`; skip bot authors.

### Branch Naming Convention

Use `<type>/<short-description>`, where type is `feat`, `fix`, `refactor`,
`docs`, `test`, `chore`, or `perf`, matching PR title prefixes.

### Commit Message Format

Use `<type>: <short summary>` (summary at most 50 characters), with an optional
body containing relevant facts and verification. Types match branch prefixes.

### When to Commit

A local commit requires human authorization. Authorized WIP checkpoints on a
personal development branch may include unfinished and experimental work after
a scoped diff review, secret and file scan, and relevant checks. Label these
commits WIP and document known failures and unverified behavior. Do not include
generated build outputs or unrelated work. Push requires human authorization.

Releases and merges to the supported branch require completed work, passing
checks, and [firmware-handoff](../skills/firmware-handoff/SKILL.md), including its
four independent reviews. A checkpoint does not satisfy this release gate.

Hardware testing is required before a PR, not before an approved local commit.
The human owns hardware results and PR prose/publication.
