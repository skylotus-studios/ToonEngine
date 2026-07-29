# Decision Records

One file per architectural decision that is expensive to reverse, numbered and dated. A record
states what was decided, what the alternatives were, and what it costs. It is not a changelog.

Write one when a choice constrains future work: a dependency, a layering boundary, a data
format, a platform commitment. Skip it for anything a later commit can undo cheaply.

## Format

Name files `NNNN-short-slug.md`, numbered in order. Each one carries:

- **Status.** Accepted, superseded (name the successor), or reversed.
- **Date.** Absolute, not relative.
- **Context.** What forced a choice.
- **Decision.** What was chosen.
- **Consequences.** What this makes easy, and what it makes hard.

## Existing History

Decisions taken before this directory existed are recorded in [MEMORY.md](../../MEMORY.md),
organised by topic, with superseded approaches and the full debugging narratives demoted to
[ARCHIVE.md](../../ARCHIVE.md). Neither is auto-loaded; read them on demand. New decisions
belong here rather than in MEMORY.md.
