---
name: update-roadmap
description: Research what to prioritize next on ToonEngine's roadmap across three lenses (architectural health, gaps blocking a Steam release, performance), promote anything that's actually shipped into docs/roadmap.md's Shipped section (including its mermaid diagram's shipped-status coloring), and update docs/roadmap.md + CLAUDE.md's roadmap pointer with the result. README.md carries its own duplicate copy of the roadmap mermaid diagram; this skill keeps it byte-for-byte structurally in sync with docs/roadmap.md's every time either changes, not `tidy-md`. Code/architecture research is grounded in docs/cpp-style-guide.md; the roadmap prose it writes follows docs/md-style-guide.md. Use when the user asks to update, refresh, reprioritize, or groom the roadmap, mark an item shipped, or "what should we work on next" in the open-ended sense. Not for planning one already-chosen item, that's `plan-roadmap`.
---

# update-roadmap: Research and Reprioritize ToonEngine's Roadmap

This skill's job: confirm the roadmap matches reality, then survey the codebase and the 
outside world, and propose additions or re-ranks for the user to accept or reject before 
anything new gets written.

This is a **triage pass across many candidates**, not a single deep design decision. Keep
each candidate's writeup tight (a few lines, not a page); depth on any one chosen item is
the skill `plan-roadmap`'s job once it's actually next in line.

## Ground Yourself First

Before researching anything new, read what's already true:

- `docs/roadmap.md` (CLAUDE.md's `## Roadmap` section is just a pointer to it) for the
  current list: a single sequence, shipped items first, then everything left ranked from
  most to least important. There's no separate lower-priority bucket for infra: every
  unshipped item, gameplay or otherwise, already sits at a specific rank, and any new or
  reprioritized candidate needs a specific rank too, not a vague "later" bucket. The mermaid
  diagram's `v0.1`-`v1.0` (then post-1.0) subgraphs are a balanced-pacing visualization layered
  on top of that same flat sequence, evenly chunking it into version-sized groups (2026-07-21
  rebalance); they are not a structural split of the ranked list itself, and `v1.0` marking the
  "official release" boundary doesn't create a second bucket either, just a label on where in
  the one sequence that boundary currently falls.
- `grep '^## ' MEMORY.md` plus the tail of its `## History` section, for what's shipped
  recently and any "Not done / deliberately deferred" notes already recorded in a topic
  section: those are prior sessions' own gap lists and count as research, not something to
  rediscover from scratch. `MEMORY.md` is organized by topic, not chronology (reorganized
  2026-07-20): `## History` is just a one-line-per-entry pointer list, the full story lives
  in each feature's own section. `ARCHIVE.md` holds full historical narratives and
  superseded material demoted out of that lookup path; it's never needed for this grounding
  step, only if the user explicitly asks for a full history.
- `git log -15` and the actual current source, to confirm the roadmap doc hasn't drifted from
  reality (an item marked pending that's actually shipped, the way CLAUDE.md's audio item was
  found stale on 2026-07-20).

Never propose an item that's already listed, already shipped, or already explicitly deferred
with a stated reason you'd just be repeating.

## Promote Anything That's Actually Shipped

The grounding check above sometimes turns up an item marked pending in "What's Next" that's
actually landed (verified against `git log` and the real current code, not a commit message
alone). When it does, promote it before researching anything new, so the rest of this pass
reasons about the real not-yet-shipped list:

1. Skip this step and follow the user's instructions if they explicitly state that a given 
   feature has completed.Confirm the full story (what was built, why, any gotchas) already lives in `MEMORY.md`under the relevant topic section (`grep '^## ' MEMORY.md` lists the current sections). If
   `docs/roadmap.md`'s entry has detail not yet in MEMORY.md, migrate it there first, don't
   delete it: losing the "why" behind a decision is the one mistake this step exists to
   prevent. Skip the migration only if the item genuinely has nothing more to say than its
   existing one-liner. **Give the feature its own new topical section** (a new `##` heading,
   placed near related sections) rather than appending a paragraph to `MEMORY.md`'s
   `## History`; add only a single short one-line entry to `## History` pointing at that new
   section. This is the exact convention `MEMORY.md`'s 2026-07-20 reorganization established
   (see its own intro and `ARCHIVE.md`'s existence) specifically to keep `History` from
   regrowing into the multi-hundred-line narrative log it used to be. If the full story is
   mostly narrative journey (a multi-round debugging saga, false starts, dead ends) rather
   than a durable design/gotcha record, put that narrative in `ARCHIVE.md` instead and leave
   only the distilled conclusion in `MEMORY.md`'s topical section, the same split
   `MEMORY.md`'s "Temporal ghosting fixes" section and `ARCHIVE.md`'s "Temporal ghosting: the
   full debugging saga" section demonstrate.
2. Move the item out of "What's Next" and into "Shipped," at the end of that section (it
   joins in the order things actually landed). If the promoted item(s) were already the
   top-ranked entry (or entries) of "What's Next" — the common case, since something worth
   shipping soon is usually already ranked near the top — **nothing below them needs
   renumbering**: their own numbers already continue the sequence correctly the moment
   Shipped's count catches up to them, so only the text block moves, not any digit.
   Renumbering is only needed when the promotion leaves an actual gap: an item promoted out of
   the *middle* of "What's Next" (everything still unshipped that was ranked below it needs to
   shift up by one to close the hole), not the ordinary top-of-list case. Getting this
   backwards, shifting the remaining list down as if closing a gap when none exists, silently
   double-assigns numbers: a real incident on 2026-07-21 did exactly this twice in a row
   (items 11-13 each ended up naming two different features) before a sequence-integrity check
   caught it — see the verification command in "Keep README's Diagram in Sync" below; run it
   after every promotion, not just after a genuine mid-list renumber. Re-check whether anything
   ranked below it should move up: a rank was often justified by what was still outstanding
   above it, and that reasoning can go stale the moment something ships.
3. Recompute the progress line (`Shipped X / Y items`) at the top of the file.
4. Update the mermaid diagram: rename the item's node id from its `N`-prefixed id to the
   `S`-prefixed id matching its new position in the Shipped count (the 8th item to ship
   becomes `S8`, wherever it sits in the diagram, in both its node-definition line and any
   arrow chain naming it), and move it out of the `class` grouping it shared with its
   still-unshipped milestone siblings into the shared `classDef shipped` style (add this
   classDef, reusing `v01`'s muted green, if the diagram doesn't have one yet). It stays
   inside its own thematic milestone subgraph (`v0.3: Interaction`, etc. — that grouping is
   chronological/thematic, not a shipped/unshipped signal) but reads as done at a glance. If
   every node in a milestone subgraph ends up shipped, that subgraph's own `classDef` can
   adopt the same shipped-green treatment wholesale, matching how `V01`/`V02` are already
   colored solid green. **Apply this exact same rename/recolor to README.md's own copy of the
   diagram in the same step** (see "Keep README's Diagram in Sync" below) — never leave the
   two diagrams to drift apart, even for one turn.
5. Update CLAUDE.md's `## Roadmap` pointer paragraph only if it no longer accurately describes
   the list's structure; re-check the 200-line cap.

This is a factual correction, not a design decision, so it doesn't wait for the
`AskUserQuestion` gate later in this skill: do it directly, then continue. It absorbs what
used to be `tidy-md`'s job for `docs/roadmap.md` specifically; `tidy-md` still owns everything
else about keeping markdown accurate, this file's own prose included.

## Keep README's Diagram in Sync

`README.md`'s `## Roadmap` section carries its own full copy of the mermaid diagram, not a
link to `docs/roadmap.md`'s. Treat the two diagrams as one logical artifact with two physical
copies: **any edit to `docs/roadmap.md`'s diagram (a promotion's rename/recolor, a new
candidate's node, a renumbering) gets mirrored into README's copy in the same pass**, never
deferred to a later `tidy-md` run. This is this skill's job specifically, not `tidy-md`'s,
because only `update-roadmap` knows the exact structural delta being made; a later pass
trying to reverse-engineer "what changed" from a diff is exactly how the two drifted apart
before. Verify after every diagram edit: `grep -oE '[SN][0-9]+' docs/roadmap.md README.md`
should report the identical id set (same ids, same S/N split) in both files, and every
`classDef`/`class` line should match structurally (colors, groupings) between the two.

**Also verify the numbering itself, not just cross-file sync.** A diagram-sync check alone can
pass while the list is still internally broken, since both files can be renumbered the same
wrong way and still match each other. After any promotion or insertion, confirm the prose
sequence is exactly 1..N with no gaps or repeats:
`grep -oE '^[0-9]+\.' docs/roadmap.md | tr -d '.' | sort -n | uniq -c | awk '$1>1{print}'` must
print nothing, and the highest number must equal the total item count (Shipped + What's Next).
Then confirm the mermaid `S`/`N` ids match those same prose numbers 1:1
(`grep -oE '[SN][0-9]+' docs/roadmap.md | sed 's/[SN]//' | sort -nu`), not merely that the two
files agree with each other.

## Research the Three Lenses, in Parallel

Launch these as parallel `Agent` calls (a `fork` or fresh general-purpose agent per lens) so
each does its own reading/searching without bloating this session's context; synthesize once
all three report back.

**a. Architectural improvements.** Grounded in `docs/cpp-style-guide.md` §7's data-oriented
discipline, not generic "refactor this" advice: unjustified encapsulation (a class with no
external dependency to quarantine and no repeated boilerplate to remove), virtual dispatch
used for a small closed enum where switch/table dispatch belongs, reflexive fragmentation
that hides a mergeable pattern. Audit real code (`src/**`) against those specific, checkable
rules, and cross-reference `docs/architecture.md` and MEMORY.md's per-system "Not done"
notes for structural rough edges a prior session already flagged but didn't act on. A finding
that guide §7 would itself reject (e.g. "add an interface for future flexibility") is not a
legitimate candidate: the guide is the filter, not a suggestion box to route around. If the 
guide wouldn't reject it, feel free to check the overall codebase and suggest if any changes
need to be made to the overall architecture to make it better, smoother, or more in principles
with the guide.

**b. Gaps blocking a Steam release.** Research what Steamworks integration and a typical
shipped Steam title actually require (achievements/stats, cloud saves, the Steam Input API,
overlay compatibility, depot/build packaging, crash reporting, a settings/graphics-options
system, save-game persistence beyond scene authoring) via web search, then cross-reference
against `docs/roadmap.md`'s Shipped section and remaining list to find what's a real gap
versus already covered (e.g. asset packaging is already on the list; don't re-propose it,
note it's covered and check whether its current rank still makes sense). This can include 
generic missing gameplay systems that fit, but don't make it specific to a genre (e.g. RTS unit picker,
playercontroller, dialogue system etc.)

**c. Performance improvements.** Start from what's already known and deferred: grep
MEMORY.md for existing notes (frustum culling of the shadow pass, instancing already on the
list, any other "Not done / deliberately deferred" perf line). Then audit hot per-frame/
per-object/per-vertex code paths against `docs/cpp-style-guide.md` §7's switch-over-virtual
and data-shape guidance (that section is explicitly about what costs real time in a hot
loop), and research how comparable stylized/indie engines handle batching, culling, and LOD
at a scale relevant to a solo project, not AAA-scale advice that doesn't apply here.

## Write Findings in cpp-style-guide Terms, Roadmap Prose in md-style-guide Terms

These are two different documents governing two different outputs of this skill, and they
don't overlap:

- Lenses (a) and (c), and any code-facing part of (b), must ground their reasoning in
  `docs/cpp-style-guide.md`'s actual rules (particularly §7). A finding that can't be traced
  to a concrete rule in that guide is an opinion, not a finding: cut it or reframe it in the
  guide's terms.
- Whatever prose actually gets written into `docs/roadmap.md` or CLAUDE.md must pass
  `docs/md-style-guide.md`: no puffery, no significance-inflation about why an item matters,
  title-case headings, straight quotes, prose over bullet-spam. Run its Pre-Output Checklist
  on anything you draft before it goes in the file.

## Synthesize a Ranked List, Then Ask Before Writing

Merge the three lenses into one candidate list, each with a proposed rank relative to the
items already in `docs/roadmap.md`. Justify every rank the same way the existing entries do
(see the file itself for the pattern): name the concrete dependency, leverage, or requirement
that puts it where it sits, not just which lens surfaced it. For each candidate, write one
paragraph: what it is, which lens it came from, the concrete rule or gap that justifies it
(name the cpp-style-guide rule, the Steam requirement, or the MEMORY.md deferred-item note),
and exactly which existing item it would slot above or below and why.

**Every candidate that clears this bar gets a real numeric rank when it's written into the
file.** There is no separate "researched but not yet ranked" holding section — a past version
of this document had one, and it was removed on 2026-07-21 by explicit user instruction: an
unranked bucket is exactly the kind of separate infra bucket CLAUDE.md's Roadmap section
already promises this list doesn't have. If a candidate genuinely doesn't clear the bar, drop
it and say why in the summary; don't park it in the file for later.

Present that list to the user before touching any file, but **default to deciding the rank
yourself** using the reasoning above, the same way every existing item's position is already
justified, rather than routing each placement back to the user as a question. Making that
judgment call is this skill's job; asking about it by default defeats the point of having done
the research (explicit user correction, 2026-07-21: "prioritize based on importance, do not
ask me, always make a judgment call"). Reserve `AskUserQuestion` for a genuinely different kind
of decision than "where does this rank": whether a *presented* candidate should be added at
all (a real accept/reject choice on a specific finding, not its placement), or a
structural/strategic call the evidence itself can't settle (e.g. whether a milestone rebalance
should reshuffle already-shipped groupings or only the unshipped tail — the user's own
preference, not something researchable). When genuinely unsure which kind of decision it is,
decide it yourself and state the reasoning plainly in the summary: a wrong rank is a one-line
fix on the next pass, and stopping to ask about something you already have enough evidence to
judge trains the user to expect friction from this skill that a competent pass shouldn't need.

## Write the Result

Only after the user has picked:

1. Update `docs/roadmap.md`: insert each chosen item at its decided rank in the single
   "What's Next" sequence, renumbering everything below it so the list still reads as one
   unbroken sequence from most to least important, with the reasoning paragraph the synthesis
   step already drafted. Recompute the progress line at the top of the file. Add the new
   item(s) to the mermaid diagram too (a new node in the right `v0.X` milestone subgraph,
   renumbering every `N`-id after it), and mirror that same diagram edit into README.md's copy
   in the same step (see "Keep README's Diagram in Sync" above) — a new candidate is exactly as
   much a sync point as a shipped-item promotion is. **Keep the milestone subgraphs roughly
   balanced in item count** (the reason for the 2026-07-21 full rebalance in the first place):
   if inserting a candidate would make one `v0.X` bucket noticeably larger than its neighbors,
   shift the boundary instead, moving its now-excess tail item into the next bucket (renumbering
   that bucket's own id as needed) rather than letting one milestone silently balloon while
   others stay small the way `v1.0: Ship` did before.
2. Update CLAUDE.md's `## Roadmap` section only if it no longer accurately describes the
   list; it stays a short pointer, not a second copy of the detail. Re-check the 200-line cap
   after editing.
3. Re-read every candidate's final wording once more against `docs/md-style-guide.md`'s
   Pre-Output Checklist before finishing.

## Non-Goals

No engine code changes. No implementation planning for a chosen item in ELI5 depth (that's
`plan-roadmap`, a separate, later ask). No adding a candidate the user didn't actually pick,
no matter how well-supported the research. No general markdown prose/staleness fixes outside
`docs/roadmap.md`/CLAUDE.md's roadmap pointer/README's roadmap mermaid diagram (everything
else in README, and the rest of markdown, is `tidy-md`'s job).