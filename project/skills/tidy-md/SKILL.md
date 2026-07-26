---
name: tidy-md
description: Keep ToonEngine's markdown docs (CLAUDE.md, README.md, docs/architecture.md, docs/roadmap.md, docs/**) accurate and current. Keeps CLAUDE.md under its hard 200-line cap, keeps README.md a portfolio-quality feature showcase (including a new highlight when a shipped item earns one), and keeps docs/architecture.md in sync with the actual abstraction-layer/pipeline/system boundaries. Also keeps MEMORY.md itself from bloating: migrates any section that's drifted into a narrative to ARCHIVE.md verbatim and leaves a compressed, still-useful technical summary in its place, so MEMORY.md stays a cheap read and ARCHIVE.md stays the complete record. Moving a shipped item into docs/roadmap.md's Shipped section, and keeping README's duplicate roadmap mermaid diagram in sync with docs/roadmap.md's, are both `update-roadmap`'s job, not this skill's. Never touches a file marked `<!-- tidy-md:locked -->`. Use when the user asks to tidy, update, or refresh documentation, right after a roadmap item ships (for the README/architecture side effects, not the roadmap-list bookkeeping itself), or when MEMORY.md is getting long and needs bloat carried to ARCHIVE.md.
---

# tidy-md: Keep ToonEngine's Markdown Docs Accurate, Current, and Right-Sized

Six documents have six different jobs. `CLAUDE.md` is a lean, always-loaded map.
`MEMORY.md` is the detailed reference, organized by topic/system, not chronology (see its
own section below for the convention). `ARCHIVE.md` is the true unlimited archive: full
historical narratives and superseded material demoted out of MEMORY.md's lookup path, never
read unless explicitly asked for. `README.md` is the public-facing pitch.
`docs/architecture.md` is the deep design reference and `docs/roadmap.md` is the deep
what's-next reference, both actively maintained alongside the other three (see their own
sections below). Don't give them all the same treatment. The rest of `docs/**` are narrower
reference docs; touch those only when they're actually wrong.

## The "Locked" Marker: Skip a File Entirely

Before reading a doc for tidying, grep it for `tidy-md:locked` (an HTML comment, invisible
when rendered, e.g. `<!-- tidy-md:locked -->`, usually the first line). **If present, skip
the file completely.** Don't re-read it "just to check," don't reformat it, don't touch it.
It means a human decided this doc is in a good, deliberate state, and automated passes are
more likely to waste effort or erase nuance than improve it. The one exception: if the user
explicitly names that file and asks for a change, do it. The marker opts a file out of
*routine* tidying, not out of an explicit direct request. To unlock a file for routine
tidying again, just delete the comment line.

Apply this marker yourself when you finish a careful, deliberate pass on a doc that isn't
going to need routine churn (a style guide, a finished setup doc for a shipped platform).
Say so in your summary so the user can veto it.

## Prose Quality: `docs/md-style-guide.md` Applies Everywhere

Before finishing any pass, check freshly-written or heavily-edited prose against
`docs/md-style-guide.md`. This is not scoped to `docs/**`; it governs every target this
skill touches, inside the docs folder and out: `CLAUDE.md`, `README.md`, `MEMORY.md`
entries, and this skill's own files just as much as anything under `docs/`. Watch especially
for its most common misses: the em dash used as a default connector instead of a period or
comma, bold used as a fake header on every list item, puffery or marketing words, and "not
just X, but Y" framing. Run its Pre-Output Checklist on anything substantial you write, not
just on `docs/**` content.

## CLAUDE.md: Hard 200-Line Cap

CLAUDE.md is loaded into every session's context, so it must stay a lean map, not an
archive. **Never let it exceed 200 lines.** Check with `wc -l CLAUDE.md` (or equivalent)
after any edit. If a genuinely new line needs to go in and the file is already near the cap,
something else has to move out or get tighter in the same edit, not "later."

CLAUDE.md's `## Roadmap` section is now just a short pointer to `docs/roadmap.md` (see that
file's own section below for where the actual detail lives). Keep that pointer paragraph
accurate to `docs/roadmap.md`'s actual structure (a single shipped-to-least-urgent list, no
milestone codenames) if it ever changes, but don't let roadmap detail creep back into
CLAUDE.md itself; that's exactly the growth this cap exists to prevent.

Re-scan the rest of CLAUDE.md while you're in there: a stale cross-reference, a "Current
state" paragraph that no longer matches what's actually built, a constraint that's been
superseded. Fix what's actually wrong; don't rewrite prose that's still accurate just to
rephrase it.

## MEMORY.md and ARCHIVE.md: Organized by Topic, Full Narratives Go to Archive

`MEMORY.md` was reorganized on 2026-07-20 (from a 2900-line file that had drifted into a
chronological diary) into sections by system/feature, each with its own heading, plus a
short `## History` section at the end that is just a one-line-per-entry pointer list, not a
narrative. `ARCHIVE.md` holds the material demoted out of that: the full original History
log verbatim, complete multi-round debugging narratives, and survey documents whose subject
matter has since shipped, kept byte-for-byte so no detail is actually lost. Nothing in
`ARCHIVE.md` needs to be read for routine engineering; it exists purely for when the user
explicitly asks for the full history behind something. Keep both files honoring this split
whenever a pass touches either one:

- A newly-shipped feature (whether written up by this skill, by `update-roadmap`'s "Promote
  Anything That's Actually Shipped" step, or by the implementation session itself) gets its
  own topical section in `MEMORY.md`, never a fresh paragraph appended to `## History`. Only
  add a single new one-line entry to `## History` pointing at that section.
- **Every pass, scan every topical section in `MEMORY.md` for bloat**, not just the section
  you came in to edit: a multi-round debugging saga told session-by-session (dead ends and
  misdiagnoses included), a superseded approach kept "for reference," a "Verified: ..." block
  running more than a few sentences, or any section that reads like a diary instead of a
  reference. Line count alone isn't the signal; staying technical and dense is fine at any
  length. "Temporal ghosting fixes" runs long precisely because it's four dense
  root-cause-and-fix write-ups with the narrative journey already stripped out.
- **When you find bloat, carry the full text to `ARCHIVE.md` before compressing anything.**
  Copy it verbatim into its own heading there first (nothing gets thinned or summarized on the
  way; that copy exists to hold literally everything), then cut `MEMORY.md`'s copy down to the
  durable conclusion: root cause, fix, current state, in whatever form stays technically dense
  but drops the play-by-play. "Temporal ghosting fixes" is the calibration target for the
  compressed side, not a one-paragraph summary. Aim for something the next session can read in
  passing without it costing much context, but that still stands on its own without the
  archive; a stub too thin to be useful is its own failure mode.
- Fix any cross-reference that assumes full narrative detail still lives in `MEMORY.md`
  itself (e.g. "see MEMORY.md's 2026-07-11 entry for the full story") to point at
  `ARCHIVE.md` instead, once that detail has actually moved there.

## README.md: Portfolio-Quality Feature Showcase

Different audience, different job. CLAUDE.md and MEMORY.md are for future engineering
sessions; README.md is for a human landing on the repo cold: a recruiter, a collaborator,
future-you. It should read as confident, scannable, and specific (name the real techniques,
like "inverted-hull outlines" or "temporal-denoised SSAO," not vague marketing words), not
as an engineering log. It's the section most exposed to `docs/md-style-guide.md`'s rules
(see above): portfolio prose is exactly where puffery and AI writing tells creep in.

When a roadmap item ships and graduates out of `docs/roadmap.md` (below), check whether it's
significant enough to be its own README highlight (a new rendering technique, a new editor
capability; not an internal refactor or a bugfix). Add it if so. Keep the section scannable
rather than exhaustive: a dense bullet list of real capabilities beats a changelog.

README's `## Roadmap` section carries its own full copy of the roadmap mermaid diagram (not a
link). Keeping that copy structurally in sync with `docs/roadmap.md`'s own diagram is
`update-roadmap`'s job, not this skill's — it owns every edit to either diagram in the same
step it makes it. Don't touch README's diagram here even if it looks out of step; that's a
sign `update-roadmap` needs a follow-up run, not something for this pass to patch around.

Refresh the hero screenshot only when the actual on-screen look has changed enough that the
old one is misleading (a new visual feature, a UI layout change), not for every minor tweak.
See the `verify` skill for how to build/launch/capture one cleanly. Save new screenshots
under `docs/screenshots/`.

Keep the Building section in sync with CLAUDE.md's Build section (same commands). If you
update one, check the other.

## docs/architecture.md: The Deep Design Reference, Actively Maintained

Unlike the rest of `docs/**` (below), `architecture.md` is a first-class maintained target,
closer to `CLAUDE.md`/`README.md` than to a narrow setup guide. It's the deep
how-it-fits-together reference: the renderer's abstraction layer, source layout, the frame loop, the
rendering pipeline, the scene model, data flow and ownership, and build/dependencies. The
canonical split across the docs, so nothing duplicates:

- `docs/architecture.md`: the deep design reference (this file).
- `docs/roadmap.md`: the deep what's-next reference (its own section below).
- `CLAUDE.md`, the lean, always-loaded map: guiding principles, conventions, short pointers
  to both of the above.
- `README.md`'s `## Architecture` section: a short summary for a reader landing on the repo
  cold, pointing to `docs/architecture.md` for the full writeup.
- `MEMORY.md`: history, reasoning, and gotchas behind individual decisions.

Update `architecture.md` in the same pass whenever the thing it describes actually changes:
the renderer abstraction layer's public API, the source layout, the frame-loop sequence, the rendering
pipeline (a new PostFX stage, a new pass), the scene model, or a roadmap item shipping that
changes what's true today (a new pipeline stage, a new engine-layer system). That's a
different trigger than the rest of `docs/**` below. A broken path/command isn't the signal
here, a changed system is. Verify against the actual current code (read the relevant
header/source; don't infer from a commit message or a roadmap checkbox), the same standard
`update-roadmap`'s own shipped-item verification holds itself to.

Keep README's `## Architecture` summary and its pointer to `architecture.md` in sync with
this file, the same way the Building sections are kept in sync (see above).

Stays **unlocked**; it tracks the code, so don't mark it `tidy-md:locked` by reflex even
after a careful pass. Locking is for content that shouldn't churn; this file is supposed to.

## docs/roadmap.md: Actively Maintained, but Ship Bookkeeping Lives Elsewhere

The deep what's-next reference: a single list, shipped items first in the order they landed,
then everything left ranked from most to least important to work on next. No milestone
codenames, no separate "someday" bucket for infra; every remaining item, gameplay or
otherwise, has an actual rank. Actively maintained, the same tier as `architecture.md`, not
part of the passive `docs/**` bucket below.

The actual ship/unship bookkeeping — confirming an item has really landed, migrating its
full story into `MEMORY.md`, moving it from "What's Next" into "Shipped," renumbering,
recomputing the progress line, and updating the mermaid diagram's shipped-status coloring —
is `update-roadmap`'s job now, not this skill's (see its "Promote Anything That's Actually
Shipped" step). This skill's remaining job for this file is the same as `architecture.md`'s:
prose-quality and staleness checks (`docs/md-style-guide.md` compliance, a broken relative
link, a stale cross-reference), not the ranked-list content itself. If a routine pass turns
up an item that looks shipped but is still listed under "What's Next," flag it and point at
`update-roadmap` rather than moving it yourself.

Stays **unlocked**, for the same reason `architecture.md` does.

## docs/**: Touch Only What's Actually Stale

For each non-locked file under `docs/` other than `architecture.md` and `roadmap.md` (both
covered above), default to leave it alone. Only edit one when:

- The user names it explicitly, or
- A quick verification turns up something actually wrong: a referenced file/command/path
  that no longer exists, a described workflow that's changed, a status banner ("planned, not
  yet built out") that's now inaccurate because the thing shipped.

Verify with greps and checks, not a re-read-for-style pass: confirm mentioned file paths
exist, confirm mentioned commands still match reality. When a doc references something
missing, that staleness cuts two ways. Don't assume the fix is always "restore the missing
thing." It might instead be "the file was deliberately removed and the doc never got updated
to stop claiming it exists." Check which one actually happened (recent commits, ask the
user, check MEMORY.md's History) before acting; recreating something a human deliberately
deleted is a worse outcome than leaving a stale doc alone for one more pass. (This happened
for real once, with `scripts/vsenv.ps1`; see ARCHIVE.md's "Full chronological history"
section, 2026-07-11 "Tooling correction" entry, for the full story.) If nothing turns up
wrong, say so and stop. Don't rewrite a doc
that's already accurate just because you looked at it. This is the point of the locked
marker's softer cousin: even unlocked docs shouldn't get touched on every pass, only when
there's a real reason.

When a new platform/system doc is genuinely needed (new authorship, not tidying), it still
follows the existing docs' voice: dense, technical, cross-referencing CLAUDE.md/MEMORY.md
rather than duplicating them.

## Finish

- Re-check every relative markdown link you touched (`[text](path)`) still resolves to a
  real file.
- `wc -l CLAUDE.md`: confirm it's 200 lines or fewer.
- If you migrated any `MEMORY.md` section to `ARCHIVE.md`, confirm the `ARCHIVE.md` copy is
  verbatim and the `MEMORY.md` copy left behind still reads as a usable reference on its own,
  not a stub.
- Summarize what moved where: what left CLAUDE.md, what (if anything) landed in MEMORY.md or
  README.md, what moved from MEMORY.md to ARCHIVE.md and how it was compressed, what changed
  in docs/architecture.md or docs/roadmap.md (prose/staleness fixes only; ship/unship
  bookkeeping is `update-roadmap`'s job), what in the rest of docs/** you
  verified-but-left-alone vs. actually changed, and any file you newly marked
  `tidy-md:locked`.