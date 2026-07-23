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
    src/core/rendering/renderer.cpp
    src/core/rendering/primitives.cpp
    src/core/scene/scene.cpp          # scene graph (uses Diligent float4x4 for world composition)
    src/core/physics/physics.cpp        # Jolt Physics backend behind the physics seam (M2.1)
    src/core/audio/audio.cpp            # miniaudio backend behind the audio seam (M2.2)
    src/core/audio/miniaudio_impl.cpp   # compiles miniaudio.h's implementation (once)
    src/core/scene/script.cpp         # native script registry + per-tick dispatch (M1.3)
    src/core/scene/scripts/spin_script.cpp   # first concrete Script (replaces the old spin block)
    src/core/camera/camera.cpp         # editor camera controls (uses Diligent float4x4 for the basis)
    src/core/input/input_system.cpp   # GLFW device polling + capture gate + callback wiring
    src/core/input/action_map.cpp     # named actions/axes, context stack, default bindings
    src/core/input/binding_io.cpp     # JSON save/load for bindings (assets/input.json)
    src/core/scene/serializer.cpp     # scene save/load (.scene text files)
    src/app/runtime_state.h      # (header-only RuntimeState struct -- the engine half, listed for IDE visibility)
    src/app/app_state.h          # (header-only AppState enum + LoadJob -- listed for IDE visibility)
    src/app/app_state.cpp        # application state machine (Boot/Title/Loading/Playing/Paused/Quit) + LoadJob drain
    src/app/runtime_tick.cpp     # engine-half per-frame tick (fixed-timestep sim + presentation), shared with the player
    src/app/runtime_render.cpp   # scene render (shadow/sky/opaque/sprite/resolve), shared with the player
    src/app/runtime_init.cpp     # runtime bring-up/teardown + the main loop (no ImGui)
    src/app/physics_glue.cpp     # collider-extent scaling + physics-world (re)construction
    src/app/audio_glue.cpp       # audio-world (re)construction from AudioSource-bearing entities
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

# We link the Vulkan engine as a SHARED library, which Diligent loads at runtime
# via LoadGraphicsEngineVk() (see renderer.cpp's `#if ENGINE_DLL` path). This define
# selects that dynamic-load path; copy_required_dlls (below) drops the engine
# DLLs next to each executable so the loader finds them.
target_compile_definitions(ToonRuntime PUBLIC ENGINE_DLL=1)

# Roadmap #10: gates the shader-hot-reload code path in core/rendering/renderer.cpp and the
# Settings panel's "Reload Now" button -- see the efsw add_subdirectory guard above for why
# this is Debug-only.
target_compile_definitions(ToonRuntime PUBLIC $<$<CONFIG:Debug>:TOON_SHADER_HOT_RELOAD>)

# Bake in the absolute path to the HLSL sources so the shader stream factory
# finds them regardless of the working directory (dev convenience). A shipped
# build would copy assets/shaders next to the exe and use a relative path. PUBLIC because the
# executables read some of these too (e.g. main.cpp/player_main.cpp use TOON_SCENES_DIR).
target_compile_definitions(ToonRuntime PUBLIC
    TOON_SHADERS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders"
    TOON_MODELS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/models"
    TOON_FONTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts"
    TOON_SPRITES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/sprites"
    TOON_ICON_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/sprites/icon.png"
    TOON_SCENES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/scenes"
    TOON_AUDIO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/audio"
    TOON_INPUT_JSON="${CMAKE_CURRENT_SOURCE_DIR}/assets/input.json"
    TOON_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets" # asset browser's root (ui/file_browser.h)
    # core/input/input_device.h needs <GLFW/glfw3.h> (GLFW_RELEASE, GLFWgamepadstate) and is
    # reachable before a TU's own local #define would run. Set engine-wide instead of per-TU so
    # it's correct regardless of #include order.
    GLFW_INCLUDE_NONE)

# Convenience function from DiligentCore: copies the engine shared libraries next to each
# executable so the OS can find and load them at runtime. Both exes need their own copy.
copy_required_dlls(ToonEngine)
copy_required_dlls(ToonPlayer)