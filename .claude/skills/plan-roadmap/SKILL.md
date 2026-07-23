---
name: plan-roadmap
description: Research and plan the next unshipped CLAUDE.md roadmap item before implementing it: checks what Diligent Engine's own modules already provide per the guiding principle, researches Diligent docs and engine-architecture best practices online, then explains the design and trade-offs in genuinely verbose, basic-CS-plus-analogy ELI5 depth before asking the user to decide. Use when the user asks to plan, scope, or design the next roadmap item, or names a specific item to plan. (ToonEngineOld, the old-engine porting reference this skill used to check first, was deleted 2026-07-21 once its three tracked ports shipped; see MEMORY.md.)
---

# plan-roadmap: Research and Plan the Next Roadmap Item

Turns one CLAUDE.md roadmap bullet into a decision-ready plan: what Diligent already
provides, what the wider ecosystem does, and a concrete design with its trade-offs explained
in plain language, so the user knows exactly what they are signing up for before approving it. The output is a plan, not engine code. This skill ends at
`ExitPlanMode`; implementation is separate, later work.

## Enter Plan Mode

Call `EnterPlanMode` first. This is exploration plus an architectural decision on a
multi-file feature, exactly what plan mode is for, and it keeps everything below from
touching a file before the user has approved anything.

## Identify the Target Item

If the user named a specific roadmap item, plan that one. Otherwise read
[docs/roadmap.md](../../../docs/roadmap.md) (CLAUDE.md's `## Roadmap` section is now just a
pointer to it): one list, shipped items first, then everything left in a single rank order
from most to least important. The target is the first item under "What's Next," full stop.
There is no separate lower-priority bucket for infra items (Linux/macOS, D3D11, shader
hot-reload, asset packaging) anymore; they're ranked inline with everything else, so treat
one exactly like any other item if it's sitting at the top of the list.

Sanity-check the pick against `git log -10` and the tail of MEMORY.md's `## History`
section. `update-roadmap` prunes shipped items out of docs/roadmap.md, but there can be a
short lag right after a commit; if the top bullet looks already built, say so and move to the
next one instead of planning something already done.

## Ground It in the Current Architecture

Skim `docs/architecture.md` (whichever sections are closest to the item: scene model for
an entity/behavior item, rendering pipeline for a rendering item) and the source files the
item would actually touch. Check MEMORY.md too: it is large and organized by topic section
(`grep '^## ' MEMORY.md` lists them) plus the dated `## History` changelog at the end, so
grep for a topic match and skim the changelog's tail rather than reading start to end. The
comparisons in the explain step need to argue in terms of this codebase's real types
(`Entity`, `Scene`, `Renderer`, the fixed sim tick), not generic engine-design language.

## Research the Item, in Parallel

Three checks. Launch b and c as parallel `Agent` calls in one message rather than one at a time. The web checks (b, c) benefit from a `fork` (or a
fresh general-purpose agent) so the raw search and fetch output does not fill this session's
context.

**a. What Diligent Engine already provides.** The load-bearing check, per CLAUDE.md's
"Guiding principle" section: build on Diligent, don't reinvent it. MEMORY.md has real
precedent for this paying off: cascaded shadow maps built on `ShadowMapManager` instead of
a hand-rolled port, asset thumbnails on `Diligent-TextureLoader`, shader hot-reload on
`IRenderStateCache`. Grep `external/DiligentCore`, `external/DiligentTools`, and especially
`external/DiligentFX/Components` (where the higher-level helpers like `ShadowMapManager`
live) for anything related to the item's keywords. Report this honestly either way. Most
gameplay-shaped items (entity behavior, physics, audio) are outside a graphics engine's job
and should come back empty; that is a real, useful answer, not a failed search. Don't force
a tie-in that isn't there.

**b. Diligent docs and samples online.** Search the official Diligent Engine repos
(`DiligentGraphics/DiligentEngine` and its wiki, `DiligentSamples`, `DiligentFX`) for
anything relevant that a local submodule grep would miss: usage patterns, an adjacent
sample, a wiki page on the relevant subsystem. Skip this (and say why) if check (b) already
gave a clear, sufficient answer for a narrowly rendering-scoped item.

**c. Engine-architecture best practices online.** Search how other engines and communities
solve this exact problem, naming real systems (Unity `MonoBehaviour`, Unreal
`Actor`/`ActorComponent`, Godot `Node`, `EnTT`/`flecs` for data-oriented ECS) rather than
generic advice, especially for solo indie developers. Filter for what actually applies given this codebase's constraints: C++17,
no ECS today, the data-encapsulated renderer abstraction layer, the fixed-timestep sim tick already shipped.

## Explain Before Asking

Lead with the explanation, before any `AskUserQuestion`. A terse options list without the
reasoning walked through first has been rejected before as premature. Walk through the
reasoning first, every time.

**The explanation must be ELI5, in layman's terms: basic CS literacy assumed, zero domain
knowledge assumed.** The reader knows what a variable, a function, a list/array, and a
struct (a bundle of named fields) are, and nothing more specific than that: not C++, not
game-engine concepts, not this codebase's existing vocabulary. This bar has been missed
twice in practice on the same feature: once by giving an analogy with no CS term attached
(too vague to pin down: "a recipe card" could mean anything), and once by giving CS terms
without enough analogy or depth (too dense to follow without domain background). Fix: do
both together, every single time a technical idea shows up. Name the basic-CS concept AND
translate it with an everyday analogy in the same breath: e.g. "a `unique_ptr`: a box
that owns what's inside it and throws it away when the box itself is destroyed; only one
box may own a given item at a time, so boxes can't be photocopied, only handed off." Never
ship the term alone or the analogy alone.

**Be verbose. Weigh every option in real depth: a sentence or a table row is not
enough.** For each real fork in the design, not every minor implementation detail, write
several sentences to a full paragraph per option, covering: what it concretely *is* (the
CS term plus the analogy, together); what it costs and what it buys, spelled out rather
than compressed into one adjective; the specific failure mode or benefit it produces in
THIS codebase, tied to something the research actually found (a real port cost from
ToonEngineOld, a real gap or fit in Diligent, a real trade-off from a specific named
engine: cite the engine/technique, never "some engines"); and a concrete sketch of what
choosing it looks like in practice later ("six months from now, adding X looks like...").
A comparison table is a supplement to that prose, never a replacement for it. A table
alone is exactly the "surface-level, could mean anything" failure this exists to prevent.
Default to this depth unprompted; being asked twice to go deeper on the same explanation
means this step under-delivered, not that the user is unusually demanding.

**Exception: once the user demonstrates fluency on a specific sub-topic themselves** (by
using its real vocabulary accurately, unprompted), match that register for that sub-topic
instead of re-explaining it ELI5: forcing basic analogies on someone who just showed
expertise reads as condescending. The ELI5 depth above is the default; a user's own
demonstrated depth overrides it locally, topic by topic, not for the rest of the plan.
At the same time, don't expect the user to understand 100% just because they know a few terms.

Only once that explanation is on the table, if a genuine decision remains, ask it with
`AskUserQuestion`. Use previews for anything concrete enough to sketch, such as two
candidate API shapes.

## Write the Plan and Exit

Write the plan file: the chosen design, the concrete steps (files touched or added, new
types, where they sit relative to the renderer's abstraction layer), and what is explicitly out of scope
for this item. Call `ExitPlanMode`. Don't start implementing inside this skill. That is the
user's next ask, a separate task.

## Non-Goals

No engine code changes, and no edits to CLAUDE.md or MEMORY.md: `update-roadmap` handles the
roadmap once the item ships. Commits are `commit`'s job. This is not a general research
report: `deep-research` is the tool for a broad, adversarially verified, multi-source
report; this skill is narrower, applied research aimed at one concrete decision.