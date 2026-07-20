---
name: tidy-md
description: Keep ToonEngine's markdown docs (CLAUDE.md, README.md, docs/architecture.md, docs/**) accurate and current. Prunes CLAUDE.md's roadmap into MEMORY.md as items ship (hard 200-line cap), keeps README.md a portfolio-quality feature showcase, and keeps docs/architecture.md in sync with the actual abstraction-layer/pipeline/system boundaries. Never touches a file marked `<!-- tidy-md:locked -->`. Use when the user asks to tidy, update, or refresh documentation, or right after a roadmap item ships.
---

# tidy-md: Keep ToonEngine's Markdown Docs Accurate, Current, and Right-Sized

Four documents have four different jobs. `CLAUDE.md` is a lean, always-loaded map.
`MEMORY.md` is the unlimited detailed archive. `README.md` is the public-facing pitch.
`docs/architecture.md` is the deep design reference, actively maintained alongside the other
three (see its own section below). Don't give them all the same treatment. The rest of
`docs/**` are narrower reference docs; touch those only when they're actually wrong.

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

## CLAUDE.md: Hard 200-Line Cap, Roadmap Flows to MEMORY.md

CLAUDE.md is loaded into every session's context, so it must stay a lean map, not an
archive. **Never let it exceed 200 lines.** Check with `wc -l CLAUDE.md` (or equivalent)
after any edit. If a genuinely new line needs to go in and the file is already near the cap,
something else has to move out or get tighter in the same edit, not "later."

The main way it grows is the Roadmap section. Whenever a roadmap item has actually shipped
(verify against recent commits and current code, not just because someone said so):

1. Confirm the full story (what was built, why, any gotchas) already lives in `MEMORY.md`
   under the relevant topic section (`## Toon pipeline`, `## Editor UI`, etc.; `grep '^## '
   MEMORY.md` shows the current section list). If CLAUDE.md's roadmap entry has detail that
   isn't in MEMORY.md yet, migrate it there first. Move it, don't delete it. Losing the
   "why" behind a decision is the one mistake this skill exists to prevent.
2. Delete the item from CLAUDE.md's roadmap. Don't leave a "done" checkbox or a stub line;
   gone means gone, MEMORY.md is the permanent record now. (This already happened once by
   hand for the editor-UI arc; see MEMORY.md's `## Editor UI` section for the shape a good
   migration takes.)
3. If a roadmap item is done and genuinely has no further detail worth archiving (a
   one-liner with nothing more to say), it's fine to just delete it. Not everything needs a
   MEMORY.md entry, only things a future session would actually want to know.

Also re-scan the rest of CLAUDE.md while you're in there, not just the roadmap: a stale
cross-reference, a "Current state" paragraph that no longer matches what's actually built, a
constraint that's been superseded. Fix what's actually wrong; don't rewrite prose that's
still accurate just to rephrase it.

## README.md: Portfolio-Quality Feature Showcase

Different audience, different job. CLAUDE.md and MEMORY.md are for future engineering
sessions; README.md is for a human landing on the repo cold: a recruiter, a collaborator,
future-you. It should read as confident, scannable, and specific (name the real techniques,
like "inverted-hull outlines" or "temporal-denoised SSAO," not vague marketing words), not
as an engineering log. It's the section most exposed to `docs/md-style-guide.md`'s rules
(see above): portfolio prose is exactly where puffery and AI writing tells creep in.

When a roadmap item ships and graduates out of CLAUDE.md (above), check whether it's
significant enough to be its own README highlight (a new rendering technique, a new editor
capability; not an internal refactor or a bugfix). Add it if so. Keep the section scannable
rather than exhaustive: a dense bullet list of real capabilities beats a changelog.

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
- `CLAUDE.md`, the lean, always-loaded map: guiding principles, conventions, the roadmap.
- `README.md`'s `## Architecture` section: a short summary for a reader landing on the repo
  cold, pointing to `docs/architecture.md` for the full writeup.
- `MEMORY.md`: history, reasoning, and gotchas behind individual decisions.

Update `architecture.md` in the same pass whenever the thing it describes actually changes:
the renderer abstraction layer's public API, the source layout, the frame-loop sequence, the rendering
pipeline (a new PostFX stage, a new pass), the scene model, or a roadmap item shipping that
changes what's true today (a new pipeline stage, a new engine-layer system). That's a
different trigger than the rest of `docs/**` below. A broken path/command isn't the signal
here, a changed system is. Verify against the actual current code (read the relevant
header/source; don't infer from a commit message or a roadmap checkbox) the same way the
CLAUDE.md roadmap check does.

Keep README's `## Architecture` summary and its pointer to `architecture.md` in sync with
this file, the same way the Building sections are kept in sync (see above).

Stays **unlocked**; it tracks the code, so don't mark it `tidy-md:locked` by reflex even
after a careful pass. Locking is for content that shouldn't churn; this file is supposed to.

## docs/**: Touch Only What's Actually Stale

For each non-locked file under `docs/` other than `architecture.md` (covered above), default
to leave it alone. Only edit one when:

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
for real once, with `scripts/vsenv.ps1`; see MEMORY.md's 2026-07-11 "Tooling correction"
entry for the full story.) If nothing turns up wrong, say so and stop. Don't rewrite a doc
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
- Summarize what moved where: what left CLAUDE.md, what (if anything) landed in MEMORY.md or
  README.md, what changed in docs/architecture.md, what in the rest of docs/** you
  verified-but-left-alone vs. actually changed, and any file you newly marked
  `tidy-md:locked`.