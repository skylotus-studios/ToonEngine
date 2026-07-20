# ToonEngine Roadmap

One list, start to finish in ranked order. There are no
calendar dates: an item moves up when something below finishes and frees it to start, or when
new research finds a reason to re-rank it. The project's guiding principle is to build on
Diligent Engine, not reinvent it.

```
Shipped  █████████░░░░░░░░░░░░░░░░  9 / 25 items
```

```mermaid
flowchart LR
    subgraph V01["v0.1: Foundation"]
        direction TB
        S1["Vulkan renderer &amp; toon pipeline"]
        S2["Dear ImGui editor"]
        S3["Editor camera &amp; input"]
        S4["Scene graph &amp; serialization"]
        S1 --> S2 --> S3 --> S4
    end

    subgraph V02["v0.2: Simulation"]
        direction TB
        S5["Fixed-timestep simulation"]
        S6["Physics: Jolt"]
        S7["Audio: miniaudio"]
        S5 --> S6 --> S7
    end

    subgraph V03["v0.3: Interaction"]
        direction TB
        S8["Mouse-pick via raycast"]
        S9["Contact events to scripts"]
        N10["Shader hot-reload"]
        S8 --> S9 --> N10
    end

    subgraph V04["v0.4: Characters &amp; World"]
        direction TB
        N11["Skeletal animation"]
        N12["Grid &amp; sky gradient"]
        N13["2D &amp; sprites"]
        N11 --> N12 --> N13
    end

    subgraph V05["v0.5: Scale &amp; Tools"]
        direction TB
        N14["Instancing"]
        N15["Frustum culling"]
        N16["Prefabs"]
        N17["Particles &amp; VFX"]
        N14 --> N15 --> N16 --> N17
    end

    subgraph V10["v1.0: Ship"]
        direction TB
        N18["Steamworks SDK bootstrap"]
        N19["Settings menu"]
        N20["Controller UI &amp; Steam Deck keyboard"]
        N21["Crash reporting"]
        N22["Asset packaging"]
        N18 --> N19 --> N20 --> N21 --> N22
    end

    subgraph V11["v1.1: Platform Expansion"]
        direction TB
        N23["Linux support"]
        N24["macOS support"]
        N25["Re-enable D3D11"]
        N23 --> N24 --> N25
    end

    V01 --> V02 --> V03 --> V04 --> V05 --> V10 --> V11

    classDef v01 fill:#5C8A7D,stroke:#3f6357,color:#EAF6F1;
    classDef v02 fill:#6B9C8C,stroke:#4a7062,color:#EAF6F1;
    classDef v03 fill:#FF6B4A,stroke:#c94e33,color:#2B0A00;
    classDef v04 fill:#FF9F40,stroke:#c97927,color:#2B1400;
    classDef v05 fill:#F4C542,stroke:#c99e28,color:#2B2100;
    classDef v10 fill:#4FA8D8,stroke:#2f7fac,color:#00202E;
    classDef v11 fill:#8A7CFF,stroke:#6357cc,color:#160B3E;
    classDef shipped fill:#5C8A7D,stroke:#3f6357,color:#EAF6F1;

    class S1,S2,S3,S4 v01
    class S5,S6,S7 v02
    class S8,S9 shipped
    class N10 v03
    class N11,N12,N13 v04
    class N14,N15,N16,N17 v05
    class N18,N19,N20,N21,N22 v10
    class N23,N24,N25 v11
```

## Shipped

1. **Vulkan renderer and toon pipeline**: cel-shaded fill + inverted-hull outline, cascaded
   shadow maps, an HDR G-buffer, and a full DiligentFX post chain (SSAO, TAA/DoF/SSR, Bloom,
   ACES tone-mapping).
2. **Dear ImGui editor**: hierarchy panel, Properties inspector with an ImGuizmo gizmo,
   Settings panel, Asset Browser with thumbnails, three themes.
3. **Editor camera and input**: orbit/pan/zoom/fly/focus camera, a rebindable action-map
   input system (mouse/keyboard/gamepad).
4. **Entity-tree scene graph and serialization**: hierarchy-composed world transforms,
   save/load to `.scene` text files.
5. **Fixed-timestep simulation**: a decoupled 60 Hz sim tick with render interpolation,
   per-entity native scripts, and Play/Step/Stop editor modes.
6. **Physics**: Jolt rigid bodies, Box/Sphere/Capsule colliders, raycasts, a collider debug
   wireframe.
7. **Audio**: positional 3D SFX and music (miniaudio), an `AudioSource` entity component.
8. **Mouse-pick**: geometric ray-vs-bounds click-to-select in the viewport, not the physics
   raycast (which only exists while Playing), with a fallback pick box for collider-less
   entities.
9. **Contact events to scripts**: `Script::OnCollisionEnter`/`OnCollisionStay`/
   `OnCollisionExit` hooks driven by a Jolt contact listener, so gameplay scripts react to
   physical contact instead of only polling transforms.

## What's Next, Most to Least Important

10. **Shader hot-reload.** Diligent already provides the mechanism
    (`IRenderStateCache`, `EnableHotReload` + `Reload()`, reachable through the linked
    `Diligent-GraphicsTools`, confirmed against Diligent's 2.5.3 release notes and
    `Tutorial26_StateCache`). Ranked here, ahead of the shader-heavy content work below it,
    because every one of those items means writing and iterating on new HLSL; paying for
    faster iteration now pays back on all of them.
11. **Skeletal animation.** Play the fox/dragon clips via an animation entity component.
    The first of three ports from `ToonEngineOld` (the old OpenGL 4.1 engine kept only as a
    porting reference); its
    `animator.cpp` (keyframe sampling, topological joint-hierarchy evaluation, bind-pose
    fallback for un-animated joints) ports close to line-for-line. Ranked ahead of the two
    items below it because animated characters are the single biggest lever for making the
    world read as an actual game instead of a static tech demo.
12. **Grid and sky gradient.** An HLSL port of `ToonEngineOld`'s editor backdrop
    (`grid.frag`'s ray-plane intersection against Y = 0, with `SV_Depth` writes so the plane
    doesn't occlude geometry between grid lines). Ranked after animation because nothing
    depends on it, but ahead of sprites because a working backdrop makes every other visual
    feature, sprites included, easier to judge by eye.
13. **2D and sprites.** A sprite entity component (tint, atlas UV rect, flip X/Y) rendered
    as a back-to-front-sorted transparent pass after the opaque toon pass, matching
    `ToonEngineOld`'s `ShadingMode::Sprite` design. Last of the three ports: sprites earn
    their place once there's a reason to place 2D elements in the world, which the two items
    above it start to create.
14. **Instancing.** A per-instance draw path for scenes with many copies of the same mesh.
    Ranked here because it only pays off once a scene actually has enough repeated objects
    to matter, which the content systems above it are what will start creating that
    scenario. `Renderer::DrawMesh`/`DrawModel` already rebind the same outline/fill PSOs on
    every entity even when they're shared, the same redundant state-setting this item's
    batching would remove, so fold that cleanup into the same pass rather than a separate one.
15. **Frustum culling: shadow cascades and the main pass.** Bounds-test each entity against
    the camera/cascade frustum before drawing it, instead of today's unconditional
    every-entity loop in both the shadow pass and the main color pass
    (`app/editor_render.cpp`'s `RenderFrame`), a gap deliberately deferred when cascaded
    shadow maps shipped) and shared, it turns out,
    by the main pass that was never named alongside it. Ranked right after instancing
    because both are scale-driven: cheap and correct to add whenever someone's already
    touching either draw loop, but nothing today measures either as an actual bottleneck at
    the current scene's object count, so it stays below every content system that outranks it.
16. **Prefabs.** Reusable entity templates for runtime spawning. A workflow multiplier: it
    cuts the cost of populating a scene with everything shipped above it, so it's ranked
    ahead of pure-visual polish that doesn't compound the same way.
17. **Particles and VFX.** A toon-appropriate particle system. Doesn't depend on anything
    above it and doesn't unlock anything below it either, so it sits after the items that do
    one or the other.
18. **Steamworks SDK bootstrap.** Link the Steamworks SDK and wire `SteamAPI_Init`/
    `SteamAPI_Shutdown`, a per-frame `SteamAPI_RunCallbacks`, a dev-time `steam_appid.txt`,
    and the overlay-activation callback. Small and mechanical, but every other Steam-specific
    item, achievements and Steam Input glyph mapping (see "Researched, Not Yet Ranked" below)
    and the controller-navigable UI item right after this one, assumes this exists first.
    Ranked ahead of the settings menu because it blocks nothing above it and opens the
    release-readiness cluster the rest of this tier belongs to.
19. **Settings menu.** A player-facing menu for display (resolution, fullscreen, VSync) and
    input (a rebind UI over the already-shipped action-map system), replacing today's
    dev-only Settings panel for anything a player, not a developer, needs to control. Steam's
    own launch checklist tests for exactly this, and ToonEngine has no player-facing display
    settings today. Should default to, or strongly favor, borderless windowed over true
    exclusive fullscreen (Vulkan titles under the Steam Overlay have documented
    exclusive-fullscreen rendering failures), and persist graphics settings per device rather
    than through cloud sync, per Valve's own Steam Deck guidance. Ranked with crash reporting
    and asset packaging below it because none of the three block or unlock anything else;
    they're release-readiness work a real release can't ship without, not features that
    create new gameplay.
20. **Controller-navigable UI and Steam Deck on-screen keyboard.** Every player-facing menu,
    starting with the settings menu this depends on, navigable end to end with a controller,
    plus `ShowGamepadTextInput`/`ShowFloatingGamepadTextInput` wired for any text entry. A
    real, separate requirement from the already-deferred Steam Input glyph mapping (that's
    which icon to show; this is whether the menu can be driven with a controller at all), and
    a genuine lever for Steam Deck Verified badging per Valve's own QA checklist. Ranked
    directly after the settings menu because it hardens the one player-facing menu system
    that item establishes, and is roughly comparable in scope to skeletal animation, not a
    small polish pass.
21. **Crash reporting.** A crash-reporting handler or SDK integration so a crash leaves a
    diagnostic trail instead of a silent exit, tested against a live endpoint before release,
    per Steam's own launch checklist. Valve's own `SteamAPI_WriteMiniDump` is documented as
    32-bit-Windows-only, so the real implementation will be a third-party service (Sentry,
    Backtrace, or a Breakpad-based one), not the Steamworks call itself. Ranked next to the
    settings menu for the same reason: a small, bounded release requirement, not a new
    subsystem, closer in shape to asset packaging below it than to any content system above it.
22. **Asset packaging for a shippable build.** Relative shader/asset paths so the engine can
    ship outside a dev environment. A genuine hard requirement before any real release, but
    small and mechanical, and it blocks nothing above it. Ranked alongside the settings menu
    and crash reporting above it, the same release-readiness cluster, without displacing the
    systems that need to exist before there's a game worth releasing.
23. **Linux support** (Vulkan). Expands the eventual audience, but Windows-only is a normal,
    viable starting point for a first release on Steam; a solo project's time before that
    point is better spent on the game itself than a second platform.
24. **macOS support** (Vulkan via MoltenVK, needs an `NSView` from a GLFW Cocoa `.mm`
    helper). Ranked after Linux because it builds on the same Vulkan-portability work Linux
    already exercises, and because it's the smaller of the two non-Windows audiences for a
    PC-first indie title.
25. **Re-enable D3D11.** Only matters for players on hardware too old for Vulkan, a small
    and shrinking slice of the Steam hardware survey. Ranked last because every item above
    it either unlocks gameplay, unlocks content, or is a hard release requirement, and this
    is none of those.

## Researched, Not Yet Ranked

Two items from the most recent `update-roadmap` pass cleared the research bar but not the
"rank it now" bar; noted here so they aren't rediscovered from scratch next time.

- **Player save/progress system**, distinct from the existing `.scene` authoring
  serialization (a checkpoint/inventory/unlocked-state format, not a full scene dump). Real
  Steam cloud-save support needs something save-shaped to sync first, but nothing shipped yet
  produces actual player state to persist, so this has no scene to attach to until a real
  gameplay loop exists.
- **Achievements/stats and Steam Input controller glyph mapping.** Confirmed real Steamworks
  features (`ISteamUserStats`, the Steam Input API), but common for a solo dev to add
  post-launch rather than at launch, and both are additive once there's actual gameplay to
  hook them into. Both now depend on item 18's Steamworks SDK bootstrap existing first.

## How This List Is Maintained

- `update-roadmap` researches what to add and where to rank it (architectural health, gaps
  blocking a Steam release, performance), proposes changes to this file, and promotes an
  item that's actually shipped: preserves its design/rationale, moves it into "Shipped," and
  renumbers the list so it still reads start to finish with no gaps.
- `plan-roadmap` takes the top not-yet-shipped item, or one the user names directly, and
  designs it in depth before implementation starts.
- `tidy-md` keeps this file's prose and cross-references accurate and current, the same
  treatment as `docs/architecture.md`, but doesn't touch the ranked-list content itself.