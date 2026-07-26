---
name: revise-style
description: Do a full, exhaustive prose-quality pass over one or more markdown (or plain-text) documents against docs/md-style-guide.md: every rule, every paragraph, the whole document, not a skim of what changed recently. Purpose-built for the failure mode where a broader tidy pass (tidy-md) fixes accuracy/structure but leaves em dashes, boilerplate phrases, and other AI writing tells behind because it's busy doing other things. Use when the user asks to fix prose quality, strip em dashes, remove AI writing tells, apply the style guide, or "polish"/"revise"/"clean up the writing in" any document: CLAUDE.md, README.md, MEMORY.md, docs/**, or any file the user names, whether or not it's part of ToonEngine's canonical doc set.
---

# revise-style: Exhaustive md-style-guide.md Enforcement

This skill has exactly one job: make the prose in the target document(s) comply with
**every** rule in `docs/md-style-guide.md`, completely, on the first pass. It does not
check facts, does not restructure content, does not prune roadmaps, does not decide what
belongs in `README.md` vs `MEMORY.md`; that's `tidy-md`'s job. This skill exists because
that broader job leaves violations behind: it's juggling accuracy, structure, and length all
at once, so a style rule it only "checks against" at the end gets a skim, not an audit. This
skill does the audit.

Read `docs/md-style-guide.md` in full before starting, even though it's summarized below.
The guide is the source of truth; this skill is a *method* for applying it without missing
things, not a paraphrase to work from instead.

## Why a Skim Isn't Good Enough

The guide has 20 checklist items across six categories, and most of them (em dashes,
boilerplate phrases, boldface spam, banned vocabulary) are the kind of thing a fast top-to-
bottom read genuinely fails to catch consistently, not because the rule is unclear, but
because pattern-matching prose for "does this sound off" and exhaustively finding every
instance of a specific character or phrase are different tasks. Em dashes in particular:
they're easy to write past because they don't look wrong locally, only in aggregate. The
fix is to stop relying on read-through alone for the objective categories and grep for them
instead. Judgment is still required for the subjective categories (puffery, formulaic
structure, negative parallelism); grep can't do those, so those still need a genuine
paragraph-by-paragraph read.

## Step 1: Resolve the Target Set

- If the user names specific file(s), use exactly those. Don't expand scope on your own.
- If the user says "the docs" or gives no target, ask which file(s) before starting rather
  than guessing. This skill's whole value is thoroughness on a defined target, and silently
  picking a scope undermines that.
- If the user explicitly asks for a full-repo sweep ("every markdown file", "the whole
  repo"), enumerate with Glob (`**/*.md`), excluding `external/**` (submodules, not this
  repo's prose) and any build output directories.
- For each target file, grep it for `tidy-md:locked` (an HTML comment, usually the first
  line). If present and the user did not explicitly name that file, skip it, following the
  same convention as `tidy-md`. If the user did name it explicitly, proceed; a direct request
  overrides the lock.

## Step 2: Mechanical Sweep (Grep First, Read Second)

Before reading a single paragraph for tone, run these searches across every target file and
collect every hit. This catches the categories that are objectively detectable and where a
prose skim is most likely to miss instances:

- **Em dashes**: search for `—`. This is the headline failure mode this skill exists to
  fix: grep every single one, don't sample. For each hit, read the sentence: if it's a
  comma, colon, or period doing the job better (the vast majority), replace it. Keep it only
  for a genuine sharp aside, and never keep two or more in the same paragraph (the guide's
  own threshold for "overuse"). Given how this skill gets invoked (existing docs already
  riddled with them), default to elimination: treat "keep as em dash" as the exception you
  have to justify, not the default.
- **Curly/smart punctuation**: search for the curly quote and apostrophe characters
  (`'` `'` `"` `"`, i.e. U+2018, U+2019, U+201C, U+201D). Replace every one with a straight
  `'` or `"`. No exceptions. The guide allows none.
- **Citation/chatbot-residue artifacts**: search for `oaicite`, `oai_citation`,
  `utm_source=chatgpt`, and reference-cluster fragments like `+1`/`+3` glued to a source name
  (e.g. `Wikipedia+1`). Delete on sight.
- **Sycophantic/assistant scraps**: search (case-insensitive) for `Certainly!`, `Of course!`,
  `Great question`, `I hope this helps`, `Let me know if`, `Feel free to`, `As an AI`,
  `Here's a breakdown`, `Honestly,`, `To be honest`, `Here's the thing`, `Genuinely`.
- **Editorializing/filler openers**: search for `It's important to note`, `It's worth
  noting`, `Notably,`, `Interestingly,`, `Needless to say`, `Importantly,`, `Moreover,`,
  `Furthermore,`, `Additionally,`, `In addition,`, `Consequently,`, `On the other hand,`,
  `That being said,`, `As such,`, `In essence,`, `Ultimately,`, `In summary`, `Overall,`,
  `In conclusion`, `To wrap up`.
- **Inflated vocabulary**: search for `delve`, `intricate`, `tapestry`, `pivotal`,
  `underscor`, `foster`, `testament`, `leverage`, `robust`, `seamless`, `navigat`,
  `multifaceted`, `nuanced`, `holistic`, `realm`, `garner`, `myriad`, `plethora`, `crucial`,
  `profound`, `transformative`, `groundbreaking`, `revolutionary`, `comprehensive`,
  `elevate`, `harness`, `embark`, `showcase`, `boasts`, `vibrant`, `bustling`,
  `state-of-the-art`, `treasure trove`, `scenic`, `breathtaking`, `nestled in the heart of`.
- **Vague attribution**: search for `some critics`, `industry experts`, `many believe`,
  `widely regarded`, `observers have noted`, `studies show`, `it has been said`.
- **Placeholders left in**: search for `[Insert`, `[Your `, `[add detail`, `[Note:`, or any
  bracketed fill-in.

Run these as one pass per file with the Grep tool (regex alternation is fine: one
multi-pattern search beats twenty single-word ones). Record every match with its line number
before fixing anything, so Step 4's re-check has a baseline to compare against.

## Step 3: Full Close Read, Paragraph by Paragraph

The mechanical sweep only covers what's grep-able. These need actual reading, section by
section, not a gestalt "does this feel okay" pass over the whole file:

- **Significance inflation**: any sentence that asserts a thing matters/is pivotal/marks a
  moment instead of stating what it does.
- **Trailing `-ing` relevance clauses**: "...cementing its legacy", "...underscoring its
  importance", etc.
- **Rule of three**: reflexive adjective triplets ("innovative, transformative, and
  groundbreaking").
- **False range**: "from X to Y" implying a spectrum that isn't real.
- **Negative parallelism**: "not just X, but Y", "more than just a...".
- **Formulaic section structure**: generic headings ("Challenges and Future Prospects")
  that don't fit the material.
- **Boldface spam**: bold used as emphasis on ordinary clauses, or as a fake header on every
  list item.
- **Lists where prose belongs**: a flowing explanation chopped into fragment bullets that
  aren't actually parallel/discrete.
- **Title case in headings**: first/last/major words capitalized, articles/conjunctions/
  short prepositions lowercase; not start case, not sentence case.
- **Self-contradictory hedging / manufactured doubt**: casting doubt on a fact stated
  plainly in the source material.

Go section by section and check each one against this list explicitly. Don't read the whole
document once and trust your overall impression. A 400-line doc with one bad paragraph in
the middle reads "fine" on a gestalt pass; it isn't.

**The guide's example phrasing for each category is an illustration, not the full spec.** A
real close read means recognizing the underlying rhetorical move, not scanning the paragraph
for those exact example words and reporting "not found" when they're absent. This skill has
already produced a false-clean report this way once: a pass checked negative parallelism by
looking for the literal string "not just X, but Y", found none, and reported the category
clean, while the same document was full of "X, not Y" and "Y, not X" doing the identical
rhetorical work in different syntax. That's Step 2's grep-shaped method leaking into Step 3,
where it doesn't belong. If you catch yourself checking a subjective category by searching
for its example string rather than reading the sentence and asking what it's doing
rhetorically, stop and actually read it.

- **Negative parallelism**, expanded: the guide's own examples ("not just X, but Y", "more
  than just a...") are the marketing-cliché end of a wider family. Also check for "X, not Y"
  and "Y, not X" (either order), "not X, Y", "not only X but Y" / "not only X, Y", and "X
  rather than Y" used for rhetorical contrast. The test is function, not syntax: is the
  rejected side (the "not X" half) a real alternative a reader would plausibly assume, being
  ruled out for a stated or evident reason? That's legitimate precision, keep it (a
  design-decision doc contrasting the shipped approach against an alternative the project
  actually considered or a past version actually did, e.g. this repo's own "data
  encapsulation, not a virtual `IRenderer`" or "built on Diligent, not a reimplementation of
  it," the latter backed by this project's own documented history of starting as exactly that
  reimplementation before pivoting). Is the rejected side a strawman nobody would assume,
  staged only to make the kept side sound more impressive by contrast? That's the banned
  pattern: drop the setup, keep the claim, per the guide's own fix. When unsure, check whether
  the document (or a sibling doc making the same claim) explains *why* the alternative was
  rejected; a real reason nearby is evidence of real content, not decoration.

## Step 4: Apply Fixes, Then Re-Sweep

Apply fixes as you find them (Edit, not a rewrite-from-scratch: preserve every fact,
number, and claim exactly; this skill changes *how* something is said, never *what* is said
or whether it's still true). Once the file has been edited:

- Re-run Step 2's full grep sweep on the edited file. Zero hits expected on every pattern
  except the deliberately-kept rare em-dash asides (should be zero or very close to it) and
  any term that's a legitimate technical word in this codebase rather than filler (e.g.
  "comprehensive" inside a proper noun, a real API name containing "Handle"). Anything else
  still matching is a miss: fix it before moving on, don't defer it.
- Skim Step 3's list once more against the edited sections specifically, since a fix to one
  sentence can introduce a new violation in the one next to it (e.g. replacing an em dash
  sometimes creates a run-on that invites a new filler transition).

## Step 5: Report

For each file touched, report: em-dash count before/after, a rough count of other violation
categories fixed, and anything you flagged but didn't change (a judgment call you're unsure
about, or a match inside a code block/URL that was a false positive and correctly left
alone). If a file was skipped for being locked, say so explicitly rather than silently
omitting it from the summary.