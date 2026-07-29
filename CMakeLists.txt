cmake_minimum_required(VERSION 3.20)

# C is needed too: DiligentTools pulls in zlib/libpng (plain-C libraries) for
# its texture loaders, which need a configured C toolchain even though
# ToonEngine's own code is C++ only.
project(ToonEngine CXX C)

# RC (Windows resource-script) compiler, for the embedded window icon below: see the
# "Application" section. Guarded on WIN32 since RC is a Windows-only concept (Linux/macOS
# are planned but don't build yet either way; this just keeps the guard honest).
if(WIN32)
    enable_language(RC)
endif()

# ---------------------------------------------------------------------------
#  ToonEngine: minimal Diligent Engine starting point
#  Rendering-only (DiligentCore) + GLFW windowing, single Vulkan backend.
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# scripts/verify.py's deep-tier `asan_sim_only` step (the agent-debug-asan preset,
# CMakePresets.json): clang-cl's own AddressSanitizer, applied globally and BEFORE every
# add_subdirectory below so the whole vendored dependency tree (Diligent, Jolt, GLFW) builds
# ASan-consistent -- a mismatched runtime between TUs is the classic way this silently stops
# catching anything. Only what clang-cl actually offers on this toolchain: no UBSan/TSan, which
# clang-cl doesn't realistically support on Windows. Off by default; every other preset is
# unaffected.
#
# Two toolchain wrinkles, both discovered by actually trying it, not guessed at:
#
# 1. clang-cl's ASan refuses to link against the DEBUG CRT ("AddressSanitizer doesn't support
#    linking with debug runtime libraries yet") -- a real, documented clang-cl limitation, not a
#    quirk to patch around with manual /MD + _ITERATOR_DEBUG_LEVEL overrides (tried first: forcing
#    /MD onto a Debug CMAKE_BUILD_TYPE still leaves `_DEBUG` defined everywhere, which independently
#    drives both _ITERATOR_DEBUG_LEVEL and the auto-selected C++ runtime import lib
#    (msvcprtd.lib), producing a cascade of ABI mismatches one flag at a time). The real fix:
#    `agent-debug-asan` (CMakePresets.json) inherits `agent-release`'s RelWithDebInfo build type
#    instead of Debug -- /MD and no `_DEBUG` come for free, correctly, everywhere, with none of
#    that by-hand patching.
#
# 2. `/fsanitize=address` is a clang-cl DRIVER flag: passing it at COMPILE time (clang-cl.exe)
#    works fine, but this project's Ninja+clang-cl setup links EVERY target -- executables and
#    shared libraries alike -- via `cmake -E vs_link_exe`/`vs_link_dll` calling `lld-link.exe`
#    DIRECTLY, bypassing the clang-cl driver that would normally translate the flag into the
#    right ASan runtime linkage. Raw lld-link has no idea what `/fsanitize=address` means and
#    fails trying to open it as a literal filename. So: no `add_link_options` here at all --
#    compile-time instrumentation stays global (below), and the actual ASan runtime import
#    library gets linked explicitly, by name, onto the specific targets that need it (see the
#    two `TOONENGINE_ASAN` blocks further down: one excludes DiligentCore's two shared-library
#    targets from instrumentation entirely, since --sim-only never loads them; the other links
#    the runtime import lib onto ToonEngine/ToonPlayer/ToonUnitTests once they exist).
option(TOONENGINE_ASAN "Build with clang-cl AddressSanitizer (/fsanitize=address)" OFF)

if(TOONENGINE_ASAN)
    add_compile_options(
        $<$<AND:$<NOT:$<STREQUAL:$<TARGET_PROPERTY:NAME>,Diligent-GraphicsEngineVk-shared>>,$<NOT:$<STREQUAL:$<TARGET_PROPERTY:NAME>,Diligent-Archiver-shared>>>:/fsanitize=address>
    )

    # Switching this preset's build type to RelWithDebInfo (CMakePresets.json, see this block's
    # own banner) turns on /O2, which is what makes vendored SPIRV-Tools' own CMakeLists.txt add
    # /GL (whole-program optimization) -- an MSVC-only flag clang-cl doesn't implement, normally
    # a harmless "unused argument" warning, except SPIRV-Tools' own CMakeLists.txt hard-codes
    # /WX (warnings-as-errors) on its MSVC branch UNCONDITIONALLY -- its own `SPIRV_WERROR`
    # option (tried first) only guards a second, redundant `/WX` append and doesn't actually
    # control this path, a real oversight in that vendored script, not something to hand-patch.
    # Suppress the specific diagnostic class instead (clang-cl still recognizes -Wno-* form):
    # nothing to escalate to /WX once the warning itself is off.
    add_compile_options(-Wno-unused-command-line-argument)
endif()

# --- Diligent Engine (git submodules under external/) ------------------------
# Core defaults to building every backend the platform can support (on
# Windows: D3D11, D3D12, OpenGL, Vulkan) regardless of which ones we actually
# link, and that's most of the compile time. ToonEngine is Vulkan-first, so disable the
# others; re-enable individually later if a specific backend is needed (e.g. D3D12 for
# RenderDoc/PIX debugging).
set(DILIGENT_NO_DIRECT3D11 ON CACHE BOOL "" FORCE)
set(DILIGENT_NO_DIRECT3D12 ON CACHE BOOL "" FORCE)
set(DILIGENT_NO_OPENGL     ON CACHE BOOL "" FORCE)

# DiligentFX also builds Radient (its real-time GI module) as a separate library.
# ToonEngine never links it, so CLion (which builds only the ToonEngine target)
# never compiled it, but a full `cmake --build` (the `all` target, i.e. CI) does,
# and it fails to compile under clang-cl (a noexcept static_assert in Radient's
# asset managers). We don't use GI, so disable it: same rationale as the backends.
set(DILIGENT_NO_RADIENT    ON CACHE BOOL "" FORCE)

# Dear ImGui: build against ToonEngine's own imgui submodule (external/imgui, pinned
# to the docking branch) instead of DiligentTools' vendored non-docking imgui.
# DiligentTools' ThirdParty/CMakeLists.txt only sets this if we haven't, so this wins;
# Diligent-Imgui then defines IMGUI_HAS_DOCK and the editor dock space (main.cpp)
# activates. Durable across clone/submodule-update.
set(DILIGENT_DEAR_IMGUI_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/imgui"
    CACHE PATH "Dear ImGui source dir (docking branch)" FORCE)

# Tools adds Dear ImGui (debug/editor UI) + texture/glTF loaders. FX adds the
# post-processing effects (bloom, tone mapping, SSAO, ...): see the toon HDR
# post chain in core/renderer.cpp.
add_subdirectory(external/DiligentCore)
add_subdirectory(external/DiligentTools)
add_subdirectory(external/DiligentFX)

# --- GLFW: cross-platform window + input (submodule under external/) ----------
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(external/glfw)

# --- Jolt Physics (submodule under external/) ---------------------------------
# M2.1 roadmap item: rigid bodies + colliders + raycasts, behind core/physics.h's seam
# (twin to core/renderer.h's Diligent seam -- see that header's banner). Jolt's own
# CMakeLists.txt lives in Build/, not the repo root; add_subdirectory points there and
# the link target it defines is named "Jolt". Its own top-level CMakeLists.txt only adds
# HelloWorld/UnitTests/Samples/etc. when IT is the root of the CMake build, which it isn't
# here, so add_subdirectory pulls in nothing but the Jolt library itself.
#
# ToonEngine only wants the core rigid-body/collision simulation, not this Jolt version's
# newer GPU-compute-backed hair/cloth simulation (Physics/Hair, Compute/DX12|VK|MTL) --
# same "disable what we don't use" rationale as the DILIGENT_NO_* block above.
#
# Jolt defaults to the STATIC MSVC runtime (/MTd in Debug) on MSVC-compatible compilers
# (which clang-cl counts as); the rest of this build (clang-cl's own default, which is what
# Diligent's libraries compile with) uses the DYNAMIC runtime (/MDd). Linking a /MTd static
# lib into a /MDd exe fails outright (lld-link: "mismatch detected for 'RuntimeLibrary'") --
# discovered by actually attempting the link, not guessed in advance. Force Jolt onto the
# dynamic runtime so every library in the final exe agrees.
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
set(JPH_USE_DX12                 OFF CACHE BOOL "" FORCE)
set(JPH_USE_VK                   OFF CACHE BOOL "" FORCE)
set(JPH_USE_MTL                  OFF CACHE BOOL "" FORCE)
set(JPH_USE_CPU_COMPUTE          OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL               OFF CACHE BOOL "" FORCE)
# Jolt defaults this ON for Release/Distribution; ToonEngine's own build has no LTO, and a
# mismatch here fails to link (Jolt's own CMakeLists.txt warns of exactly this scenario).
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
add_subdirectory(external/JoltPhysics/Build)

# --- miniaudio (submodule under external/) ------------------------------------
# M2 roadmap item: SFX + music, positional, behind core/audio/audio.h's seam (twin to
# core/physics/physics.h's Jolt seam). miniaudio is a single header (no CMakeLists to
# add_subdirectory) -- its include dir is added to the target below, and
# src/core/audio/miniaudio_impl.cpp is the one TU that compiles its implementation
# (MINIAUDIO_IMPLEMENTATION) -- see that file's own banner for why it's split out.

# --- efsw (submodule under external/), Debug builds only ----------------------
# Roadmap #10 (shader hot-reload): a cross-platform (Windows/Linux/macOS) file-system
# watcher, used to notice a changed .hlsl file and trigger Diligent's own
# IRenderStateCache::Reload() (core/rendering/renderer.cpp) -- no engine-side per-platform
# watcher code needed. Diligent's own docs say hot-reload machinery "should generally be
# disabled in production builds," so efsw is only built/linked for CMAKE_BUILD_TYPE Debug
# (windows-debug preset); windows-release (RelWithDebInfo) never touches this block, and
# TOON_SHADER_HOT_RELOAD (compile definition below) is what core/rendering/renderer.cpp and
# ui/panels/settings_panel.cpp gate their hot-reload code on.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(BUILD_TEST_APP OFF CACHE BOOL "" FORCE) # efsw's own file-watch demo exe -- don't need it
    set(EFSW_INSTALL   OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/efsw)
endif()

# --- Application -------------------------------------------------------------
# imgui_impl_glfw.cpp is Dear ImGui's GLFW platform backend. DiligentTools'
# Imgui module only auto-compiles the Win32/Linux/macOS-native/SDL backends
# (it doesn't know we use GLFW), so we compile this one file ourselves.
# DILIGENT_DEAR_IMGUI_PATH is set above to ToonEngine's own imgui submodule
# (external/imgui, docking branch), not DiligentTools' vendored one.
# ToonRuntime: the engine + the standalone runtime, with NO editor (roadmap #15). Both
# executables below link it; ToonPlayer links ONLY it, so a shipped game contains none of
# ui/panels/ or ImGuizmo. Every core/ TU plus the app/ runtime half (runtime_*, app_state)
# lives here; the editor-only app/ TUs (editor_*, picking, scene_ops) live in ToonEngine.
add_library(ToonRuntime STATIC
    src/core/platform/paths.h         # (header-only interface, listed for IDE visibility)
    src/core/platform/paths.cpp       # exe-relative asset path resolution (roadmap #16)
    src/core/platform/clock.h         # (header-only interface, listed for IDE visibility)
    src/core/platform/clock.cpp       # monotonic wall clock; what freed the sim half from GLFW
    src/core/platform/memstats.h      # (header-only interface, listed for IDE visibility)
    src/core/platform/memstats.cpp    # process peak working-set size (metrics.json's alloc.peak_bytes)
    src/core/rendering/renderer.cpp
    src/core/rendering/primitives.cpp
    src/core/ui/text.h                # (header-only interface, listed for IDE visibility)
    src/core/ui/text.cpp              # MSDF font load + text layout for the in-game UI (roadmap #17)
    src/core/ui/ui.h                  # (header-only interface, listed for IDE visibility)
    src/core/ui/ui.cpp                # Fleury immediate-over-cache UI core: box cache + layout + signals (roadmap #17)
    src/core/ui/strings.h             # (header-only interface, listed for IDE visibility)
    src/core/ui/strings.cpp           # player-facing string-key table (roadmap #17, localization-ready)
    src/core/scene/scene.cpp          # scene graph (uses Diligent float4x4 for world composition)
    src/core/physics/physics.cpp        # Jolt Physics backend behind the physics seam (M2.1)
    src/core/audio/audio.cpp            # miniaudio backend behind the audio seam (M2.2)
    src/core/audio/miniaudio_impl.cpp   # compiles miniaudio.h's implementation (once)
    src/core/scene/script.cpp         # native script registry + per-tick dispatch (M1.3)
    src/core/scene/scripts/spin_script.cpp   # first concrete Script (replaces the old spin block)
    src/core/scene/scripts/level_exit_script.cpp # collision-triggered level change (roadmap #19)
    src/core/scene/scripts/builtin_scripts.cpp   # explicit registration of every built-in script
    src/core/camera/camera.cpp         # editor camera controls (uses Diligent float4x4 for the basis)
    src/core/input/input_system.cpp   # GLFW device polling + capture gate + callback wiring
    src/core/input/action_map.cpp     # named actions/axes, context stack, default bindings
    src/core/input/binding_io.cpp     # JSON save/load for bindings (assets/input.json)
    src/core/scene/serializer.cpp     # scene save/load (.scene text files)
    src/core/save/savegame.cpp        # player save/progress file (roadmap #18), distinct from .scene
    src/app/save_glue.cpp        # bridges SaveGame <-> RuntimeState (MakeSave/ApplySave/QuickSave/QuickLoad)
    src/app/runtime_state.h      # (header-only RuntimeState struct -- the engine half, listed for IDE visibility)
    src/app/app_state.h          # (header-only AppState enum + LoadJob -- listed for IDE visibility)
    src/app/app_state.cpp        # application state machine (Boot/Title/Loading/Playing/Paused/Quit) + LoadJob drain
    src/app/runtime_tick.cpp     # engine-half per-frame tick (fixed-timestep sim + presentation), shared with the player
    src/app/runtime_render.cpp   # scene render (shadow/sky/opaque/sprite/resolve), shared with the player
    src/app/runtime_ui.h         # (header-only interface, listed for IDE visibility)
    src/app/runtime_ui.cpp       # in-game HUD/menu driver (roadmap #17), shared with the player
    src/app/runtime_init.cpp     # runtime bring-up/teardown + the main loop (no ImGui)
    src/app/sim_runtime.h        # (header-only interface, listed for IDE visibility)
    src/app/sim_runtime.cpp      # --sim-only: the headless sim harness (no window, no device, no audio)
    src/app/world_hash.h         # (header-only interface, listed for IDE visibility)
    src/app/world_hash.cpp       # HashWorldState: a 64-bit fingerprint of simulated state (--hash-every)
    src/app/metrics.h            # (header-only interface, listed for IDE visibility)
    src/app/metrics.cpp          # WriteMetrics: emits artifacts/metrics.json
    src/app/headless_render.h    # (header-only interface, listed for IDE visibility)
    src/app/headless_render.cpp  # --headless-render: real device, hidden window, strict validation, PNG capture
    src/app/scene_roundtrip.h    # (header-only interface, listed for IDE visibility)
    src/app/scene_roundtrip.cpp  # --scene-roundtrip: load/save idempotency check (scripts/verify.py's fast tier)
    src/app/resize_soak.h        # (header-only interface, listed for IDE visibility)
    src/app/resize_soak.cpp      # --resize-soak: repeated Renderer::Resize under a real device (scripts/verify.py's deep tier)
    src/app/physics_glue.cpp     # collider-extent scaling + physics-world (re)construction
    src/app/audio_glue.cpp       # audio-world (re)construction from AudioSource-bearing entities
    src/app/session.h            # (header-only interface, listed for IDE visibility)
    src/app/session.cpp          # gameplay session + scene-transition lifecycle (roadmap #19)
    # renderer.cpp calls ImGui_ImplGlfw_* (InitUI/BeginUI/EndUI): the GLFW backend TU lives here
    # so the runtime resolves those symbols. The player links it but simply never calls InitUI,
    # so no ImGui frame is ever built (the one residual seam the plan notes, splitting it out is
    # a separate item).
    ${DILIGENT_DEAR_IMGUI_PATH}/backends/imgui_impl_glfw.cpp
)

# ToonEngine: the editor. Links ToonRuntime and adds the editor-only entry point + UI + gizmo.
add_executable(ToonEngine
    src/main.cpp
    src/app/editor_state.h       # (header-only EditorState struct -- listed for IDE visibility)
    src/app/editor_init.cpp      # one-time editor setup + the demo scene
    src/app/editor_tick.cpp      # editor camera nav (wraps runtime_tick)
    src/app/editor_render.cpp    # editor render trailers: grid, collider + pick-marker overlays (wraps runtime_render)
    src/app/picking.cpp          # click-to-select: screen-ray vs. entity world bounds (editor-only)
    src/app/scene_ops.cpp        # scene load/save/new (File menu + asset-browser double-click)
    src/ui/thumbnail_cache.cpp  # path -> texture cache (asset browser icons/preview)
    src/ui/panels/file_browser.cpp     # "Asset Browser" editor panel
    src/ui/panels/themes.cpp    # editor color themes (Amber Yellow / Gruvbox Hard / Gray Stone)
    src/ui/panels/menu_bar.cpp      # File / Edit / Tools / View / Help + their modals
    src/ui/panels/dockspace.cpp     # full-window dockspace + one-time default panel layout
    src/ui/panels/playback_panel.cpp    # Play / Pause / Step / Stop
    src/ui/panels/objects_panel.cpp     # "Objects" hierarchy panel
    src/ui/panels/properties_panel.cpp  # "Properties" inspector
    src/ui/panels/gizmo_overlay.cpp     # gizmo hotkeys + the ImGuizmo transform overlay
    src/ui/panels/settings_panel.cpp    # "Settings" panel (shading/camera/physics/post-fx)
    external/ImGuizmo/src/ImGuizmo.cpp   # editor transform gizmos (compiled against Diligent's imgui)
)
target_link_libraries(ToonEngine PRIVATE ToonRuntime)
# Editor-only include roots: ImGuizmo (the transform gizmo) + IconFontCppHeaders (the asset
# browser's Font Awesome glyph macros). The runtime needs neither.
target_include_directories(ToonEngine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/external/ImGuizmo/src
    ${CMAKE_CURRENT_SOURCE_DIR}/external/IconFontCppHeaders)

# ToonPlayer: the shippable game runtime. Links ONLY ToonRuntime -- the linker strips the editor
# by reachability, since player_main.cpp never names a ui/panels/ symbol.
add_executable(ToonPlayer src/app/player_main.cpp)
target_link_libraries(ToonPlayer PRIVATE ToonRuntime)

# ToonUnitTests: scripts/verify.py's fast-tier `unit_tests` step and deep-tier `savegame_compat`
# step (tests/unit/test_framework.h's own banner: a hand-rolled registry, no gtest/Catch2 -- no
# new dependency, matching this repo's "no vcpkg" rule 5). Links ToonRuntime for
# core/save/savegame.h's symbols; core/math.h is header-only and needs none, but shares the same
# include root. Two CTest entries filter the same binary by name substring rather than building
# two executables -- see test_main.cpp's --filter handling.
enable_testing()
add_executable(ToonUnitTests
    tests/unit/test_framework.h
    tests/unit/test_main.cpp
    tests/unit/math_tests.cpp
    tests/unit/savegame_tests.cpp
)
target_link_libraries(ToonUnitTests PRIVATE ToonRuntime)
target_include_directories(ToonUnitTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/unit)
add_test(NAME ToonUnitTests_Math COMMAND ToonUnitTests --filter=Math)
add_test(NAME ToonUnitTests_SaveGame COMMAND ToonUnitTests --filter=SaveGame)

# TOONENGINE_ASAN, part 2 (see this file's earlier banner, right after CMAKE_CXX_STANDARD_REQUIRED,
# for why this isn't a plain add_link_options(/fsanitize=address)): link the ASan runtime import
# library explicitly, by name, onto every target that produces a FINAL linked artifact under ASan
# -- not just the three executables, but also DiligentCore's two shared libraries
# (Diligent-GraphicsEngineVk-shared, Diligent-Archiver-shared). Those two are excluded from
# INSTRUMENTATION (no /fsanitize=address on their own compile, see the exclusion above), but they
# still LINK against many instrumented static libraries (GraphicsEngine, Common, BasicPlatform,
# ...), which reference ASan runtime symbols (e.g. __asan_shadow_memory_dynamic_address) that
# only the runtime import library resolves -- excluding a target from instrumentation doesn't
# exempt it from satisfying symbols its own dependencies pull in. Static libraries need none of
# this: they have no link-time symbol resolution step of their own. Matches /MD (the dynamic
# release CRT this preset forces): the DYNAMIC ASan runtime, not the static one.
if(TOONENGINE_ASAN)
    file(GLOB TOONENGINE_ASAN_RUNTIME_LIB
        "C:/Program Files/LLVM/lib/clang/*/lib/windows/clang_rt.asan_dynamic-x86_64.lib")
    file(GLOB TOONENGINE_ASAN_RUNTIME_DLL
        "C:/Program Files/LLVM/lib/clang/*/lib/windows/clang_rt.asan_dynamic-x86_64.dll")
    # The dynamic import lib above only exports the INDIRECT `__imp_*` symbols; instrumented
    # code compiled as if the runtime were in the SAME module (clang's normal assumption)
    # references the DIRECT symbol (e.g. `__asan_shadow_memory_dynamic_address`, confirmed via
    # llvm-nm: only `__imp___asan_shadow_memory_dynamic_address` is exported, not the plain
    # name). LLVM's documented fix for "runtime lives in a separate DLL": also link the runtime
    # THUNK library, force-included with /WHOLEARCHIVE since its symbols aren't otherwise pulled
    # in by ordinary reference resolution.
    file(GLOB TOONENGINE_ASAN_RUNTIME_THUNK
        "C:/Program Files/LLVM/lib/clang/*/lib/windows/clang_rt.asan_dynamic_runtime_thunk-x86_64.lib")
    list(GET TOONENGINE_ASAN_RUNTIME_LIB 0 TOONENGINE_ASAN_RUNTIME_LIB)
    list(GET TOONENGINE_ASAN_RUNTIME_DLL 0 TOONENGINE_ASAN_RUNTIME_DLL)
    list(GET TOONENGINE_ASAN_RUNTIME_THUNK 0 TOONENGINE_ASAN_RUNTIME_THUNK)
    set(TOONENGINE_ASAN_LINK_FLAGS
        "${TOONENGINE_ASAN_RUNTIME_LIB}" "/WHOLEARCHIVE:${TOONENGINE_ASAN_RUNTIME_THUNK}")
    # Diligent-GraphicsEngineVk-shared/Diligent-Archiver-shared are defined inside
    # external/DiligentCore's own subdirectory scope, not here -- target_link_libraries works
    # across directory scopes for an existing target, but add_custom_command(TARGET ...) does
    # not (a hard CMake restriction: it can only attach to a target defined in the CURRENT
    # directory). They don't need their own copy of the ASan runtime DLL anyway: every target
    # here shares the same CMAKE_RUNTIME_OUTPUT_DIRECTORY, so the copy the executables' loop
    # below performs already lands it in the one directory all of these load from.
    foreach(_toon_asan_link_only_target Diligent-GraphicsEngineVk-shared Diligent-Archiver-shared)
        # target_link_options, not target_link_libraries: CMake's LINK_LIBRARIES ordering
        # interleaves a plain-path library with the target's PUBLIC/transitively-propagated
        # dependencies (topologically resolved from the static libs it depends on), which can
        # place it BEFORE the object files that actually reference its symbols -- lld-link, like
        # link.exe, resolves archive/import-lib symbols in a single left-to-right pass, so a
        # runtime support library needs to be LAST, not wherever LINK_LIBRARIES ordering happens
        # to put it. target_link_options appends straight onto the end of the raw command line.
        target_link_options(${_toon_asan_link_only_target} PRIVATE ${TOONENGINE_ASAN_LINK_FLAGS})
    endforeach()

    foreach(_toon_asan_target ToonEngine ToonPlayer ToonUnitTests)
        target_link_options(${_toon_asan_target} PRIVATE ${TOONENGINE_ASAN_LINK_FLAGS})
        # The ASan DLL itself (loaded at runtime, same as copy_required_dlls does for Diligent's
        # engine DLLs below) has to sit next to the exe, not just its import library at link time.
        add_custom_command(TARGET ${_toon_asan_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${TOONENGINE_ASAN_RUNTIME_DLL}" "$<TARGET_FILE_DIR:${_toon_asan_target}>")
    endforeach()
endif()

# Embed the window icon into the .exe itself (RT_GROUP_ICON/RT_ICON resources), not just
# set it at runtime (core/renderer.cpp's SetWindowIcon, called from main.cpp, still does
# that too, for WM_SETICON on the live window). GLFW's Win32 backend looks for a resource
# named exactly GLFW_ICON when it registers the window class (see
# external/glfw/src/win32_window.c) and falls back to the generic system icon if it can't
# find one; that fallback is what the taskbar/Alt-Tab/shell were showing even though the
# runtime call had already fixed the title bar. assets/icons/icon.ico wraps assets/sprites/icon.png in a
# minimal ICO container (a single PNG-compressed frame, supported since Windows Vista).
if(WIN32)
    set(TOON_ICON_ICO_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/icon.ico")
    configure_file(src/icon.rc.in ${CMAKE_CURRENT_BINARY_DIR}/icon.rc @ONLY)
    target_sources(ToonEngine PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/icon.rc)
    target_sources(ToonPlayer PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/icon.rc)
endif()

# All of the following (include roots, links, defines) attach to ToonRuntime as PUBLIC, so both
# executables that link it inherit them. Only the editor-only ImGuizmo/IconFontCppHeaders roots
# are excluded (they're on ToonEngine above). src/ is the include root: cross-directory includes
# read "core/renderer.h". miniaudio is single-header (no CMakeLists of its own).
target_include_directories(ToonRuntime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/external/miniaudio)

# core/renderer.cpp includes DiligentFX's C++-side shader-structure headers
# (BasicStructures.fxh -> CameraAttribs, BloomStructures.fxh -> BloomAttribs) to
# talk to PostFXContext/Bloom. The DiligentFX target already exposes its root for
# the full-path includes, but BasicStructures.fxh does a *bare*
# #include "ShaderDefinitions.fxh", so put that directory on the include path too.
target_include_directories(ToonRuntime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/external/DiligentFX/Shaders/Common/public)

# core/input/binding_io.cpp persists key bindings as JSON via the nlohmann/json library
# DiligentTools already vendors (external/DiligentTools/ThirdParty/json). Diligent-JSON's own
# INTERFACE include dir only exposes the "nlohmann" leaf (single_include/nlohmann), which
# would give #include <json.hpp>; adding the parent directory too keeps the conventional
# #include <nlohmann/json.hpp> spelling.
target_include_directories(ToonRuntime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/external/DiligentTools/ThirdParty/json/single_include)

# Diligent-BuildSettings is an INTERFACE target that propagates the platform
# defines (PLATFORM_WIN32 / PLATFORM_LINUX / PLATFORM_MACOS) and compiler flags
# the headers need. Link it first. PUBLIC so the executables inherit every usage requirement.
target_link_libraries(ToonRuntime
    PUBLIC
        Diligent-BuildSettings
        Diligent-GraphicsEngineVk-shared   # Vulkan: Windows, Linux, macOS (via MoltenVK)
        Diligent-Common                    # RefCntAutoPtr.hpp + other Common/interface utilities
        Diligent-GraphicsTools             # MapHelper.hpp (dynamic CB updates) + graphics utilities
        Diligent-Archiver-shared           # ArchiverFactoryLoader.h (roadmap #10, shader hot-reload):
                                            # GraphicsTools links Diligent-ArchiverInterface PRIVATE, so
                                            # it does NOT propagate to us -- link the shared (DLL)
                                            # variant explicitly instead of just the interface target,
                                            # the same LoadEngineDll("Archiver", ...) runtime-load
                                            # pattern ENGINE_DLL already uses for the Vulkan engine
                                            # factory (its own DILIGENT_ARCHIVER_SHARED=1 PUBLIC define
                                            # is what selects that path in ArchiverFactoryLoader.h;
                                            # linking Diligent-ArchiverInterface alone leaves the code on
                                            # the static-link path, which then fails at link time with
                                            # no Diligent-Archiver-static in the build). The DLL itself
                                            # is already built (DILIGENT_NO_ARCHIVER unset) and
                                            # copy_required_dlls already copies it automatically.
        Diligent-AssetLoader               # GLTF::Model: glTF/GLB loading (self-contained; no PBR renderer)
        Diligent-TextureLoader             # CreateTextureFromFile + image decode (PNG/JPG)
        Diligent-Imgui                     # ImGuiImplDiligent renderer backend + vendored Dear ImGui
        Diligent-JSON                      # nlohmann/json for input-bindings persistence (see below).
                                            # Diligent-AssetLoader links this PRIVATE, so it does NOT
                                            # propagate transitively: must be linked explicitly here.
        DiligentFX                         # post-processing effects (bloom, tone mapping, ...)
        glfw
        Jolt                                # rigid-body physics + collision (M2.1) -- also brings
                                            # Jolt's own PUBLIC include dir + compile definitions
                                            # (instruction-set flags etc.) along transitively.
)

# efsw only builds in Debug (see the add_subdirectory guard above) -- link it in the same guard.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_link_libraries(ToonRuntime PUBLIC efsw-static)
endif()

# Native title-bar theming (core/renderer.cpp's SetTitleBarTheme, via DWM) needs
# dwmapi.lib. Windows-only, like the icon/RC block above -- Linux/macOS don't build yet.
if(WIN32)
    target_link_libraries(ToonRuntime PUBLIC dwmapi)
endif()

# core/platform/memstats.cpp's PeakWorkingSetBytes (app/metrics.h's alloc.peak_bytes) needs
# GetProcessMemoryInfo, declared in psapi.h. Same Windows-only shape as the dwmapi link above.
if(WIN32)
    target_link_libraries(ToonRuntime PUBLIC Psapi)
endif()

# We link the Vulkan engine as a SHARED library, which Diligent loads at runtime
# via LoadGraphicsEngineVk() (see renderer.cpp's `#if ENGINE_DLL` path). This define
# selects that dynamic-load path; copy_required_dlls (below) drops the engine
# DLLs next to each executable so the loader finds them.
target_compile_definitions(ToonRuntime PUBLIC ENGINE_DLL=1)

# Roadmap #10: gates the shader-hot-reload code path in core/rendering/renderer.cpp and the
# Settings panel's "Reload Now" button -- see the efsw add_subdirectory guard above for why
# this is Debug-only.
target_compile_definitions(ToonRuntime PUBLIC $<$<CONFIG:Debug>:TOON_SHADER_HOT_RELOAD>)

# Development fallback ONLY: the absolute path to the source-tree assets/ directory. At
# runtime the engine prefers an assets/ directory sitting next to the exe (a packaged build,
# staged by the install() rules below) and resolves it there via core/platform/paths.cpp's
# Assets::Init(), independent of the working directory. This baked path is consulted only when
# that folder isn't present -- i.e. running straight out of build/<preset>/ during development,
# where nothing copied assets next to the exe. PUBLIC because both executables link paths.cpp
# through ToonRuntime.
target_compile_definitions(ToonRuntime PUBLIC
    TOON_ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    # core/input/input_device.h needs <GLFW/glfw3.h> (GLFW_RELEASE, GLFWgamepadstate) and is
    # reachable before a TU's own local #define would run. Set engine-wide instead of per-TU so
    # it's correct regardless of #include order.
    GLFW_INCLUDE_NONE)

# Convenience function from DiligentCore: copies the engine shared libraries next to each
# executable so the OS can find and load them at runtime. Both exes need their own copy.
copy_required_dlls(ToonEngine)
copy_required_dlls(ToonPlayer)

# --- Packaging (roadmap #16): stage a self-contained, relocatable build ------------------
# `cmake --install build/<preset> --prefix <dir> --component toonengine` assembles <dir>/ with
# both exes, the assets/ tree, and the engine DLLs all together. Because the engine resolves
# assets/ next to the exe at runtime (core/platform/paths.cpp), that staged folder runs from
# anywhere -- a test dir, another machine, a Steam install -- with no configuration. This is the
# artifact SteamPipe upload (roadmap #29) and the packaged-build smoke test (#30) build on.
# install() only records a packing list; it adds nothing to an ordinary `cmake --build`.
#
# Every rule is tagged COMPONENT toonengine, and the package step passes `--component
# toonengine`, so ONLY these three rules run -- the vendored submodules (Diligent, GLFW, efsw)
# register their own install() rules for headers/static libs/licenses, and without the
# component filter `cmake --install` would dump all of them into the game folder too.
install(TARGETS ToonEngine ToonPlayer RUNTIME DESTINATION . COMPONENT toonengine)
install(DIRECTORY assets/ DESTINATION assets COMPONENT toonengine)
# The engine's Vulkan/Archiver DLLs are the ones copy_required_dlls already dropped, flat, next
# to the build exe (Diligent's own helper knows which shared libraries the runtime LoadLibrary's
# -- $<TARGET_RUNTIME_DLLS> wouldn't catch the dynamically loaded engine DLL, and it needs CMake
# 3.21 anyway; this repo pins 3.20). Glob those flat *.dll at INSTALL time -- after the build,
# so they exist, and with no directory recursion, so none of the build tree's subdirectories
# come along. Captures whichever suffix the config produced (debug _64d / release _64).
install(CODE "
    file(GLOB _toon_dlls \"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/*.dll\")
    file(INSTALL \${_toon_dlls} DESTINATION \"\${CMAKE_INSTALL_PREFIX}\")
" COMPONENT toonengine)