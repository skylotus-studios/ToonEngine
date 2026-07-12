cmake_minimum_required(VERSION 3.20)

# C is needed too: DiligentTools pulls in zlib/libpng (plain-C libraries) for
# its texture loaders, which need a configured C toolchain even though
# ToonEngine's own code is C++ only.
project(ToonEngine CXX C)

# ---------------------------------------------------------------------------
#  ToonEngine — minimal Diligent Engine starting point
#  Rendering-only (DiligentCore) + GLFW windowing, single Vulkan backend.
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Diligent Engine (git submodules under external/) ------------------------
# Core defaults to building every backend the platform can support (on
# Windows: D3D11, D3D12, OpenGL, Vulkan) regardless of which ones we actually
# link — that's most of the compile time. ToonEngine is Vulkan-first (see
# CLAUDE.md roadmap), so disable the others; re-enable individually later if a
# specific backend is needed (e.g. D3D12 for RenderDoc/PIX debugging).
set(DILIGENT_NO_DIRECT3D11 ON CACHE BOOL "" FORCE)
set(DILIGENT_NO_DIRECT3D12 ON CACHE BOOL "" FORCE)
set(DILIGENT_NO_OPENGL     ON CACHE BOOL "" FORCE)

# DiligentFX also builds Radient (its real-time GI module) as a separate library.
# ToonEngine never links it, so CLion (which builds only the ToonEngine target)
# never compiled it — but a full `cmake --build` (the `all` target, i.e. CI) does,
# and it fails to compile under clang-cl (a noexcept static_assert in Radient's
# asset managers). We don't use GI, so disable it — same rationale as the backends.
set(DILIGENT_NO_RADIENT    ON CACHE BOOL "" FORCE)

# Tools adds Dear ImGui (debug/editor UI) + texture/glTF loaders. FX adds the
# post-processing effects (bloom, tone mapping, SSAO, ...) — see the toon HDR
# post chain in core/renderer.cpp.
add_subdirectory(external/DiligentCore)
add_subdirectory(external/DiligentTools)
add_subdirectory(external/DiligentFX)

# --- GLFW: cross-platform window + input (submodule under external/) ----------
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(external/glfw)

# --- Application -------------------------------------------------------------
# imgui_impl_glfw.cpp is Dear ImGui's GLFW platform backend. DiligentTools'
# Imgui module only auto-compiles the Win32/Linux/macOS-native/SDL backends
# (it doesn't know we use GLFW), so we compile this one file ourselves.
# DILIGENT_DEAR_IMGUI_PATH is set by DiligentTools' ThirdParty/CMakeLists.txt
# above to point at the vendored imgui submodule.
add_executable(ToonEngine
    src/main.cpp
    src/core/renderer.cpp
    src/core/primitives.cpp
    src/core/scene.cpp          # scene graph (uses Diligent float4x4 for world composition)
    src/core/camera.cpp         # editor camera controls (uses Diligent float4x4 for the basis)
    src/core/input.cpp          # GLFW input polling + capture gate
    src/core/serializer.cpp     # scene save/load (.scene text files)
    ${DILIGENT_DEAR_IMGUI_PATH}/backends/imgui_impl_glfw.cpp
    external/ImGuizmo/src/ImGuizmo.cpp   # editor transform gizmos (compiled against Diligent's imgui)
)

# src/ is the include root: cross-directory includes read "core/renderer.h".
# ImGuizmo/src so main.cpp (and ImGuizmo.cpp) can #include "ImGuizmo.h"; ImGuizmo itself
# pulls in imgui.h/imgui_internal.h, which come transitively from the Diligent-Imgui link.
target_include_directories(ToonEngine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/external/ImGuizmo/src)

# core/renderer.cpp includes DiligentFX's C++-side shader-structure headers
# (BasicStructures.fxh -> CameraAttribs, BloomStructures.fxh -> BloomAttribs) to
# talk to PostFXContext/Bloom. The DiligentFX target already exposes its root for
# the full-path includes, but BasicStructures.fxh does a *bare*
# #include "ShaderDefinitions.fxh", so put that directory on the include path too.
target_include_directories(ToonEngine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/external/DiligentFX/Shaders/Common/public)

# Diligent-BuildSettings is an INTERFACE target that propagates the platform
# defines (PLATFORM_WIN32 / PLATFORM_LINUX / PLATFORM_MACOS) and compiler flags
# the headers need. Link it first.
target_link_libraries(ToonEngine
    PRIVATE
        Diligent-BuildSettings
        Diligent-GraphicsEngineVk-shared   # Vulkan: Windows, Linux, macOS (via MoltenVK)
        Diligent-Common                    # RefCntAutoPtr.hpp + other Common/interface utilities
        Diligent-GraphicsTools             # MapHelper.hpp (dynamic CB updates) + graphics utilities
        Diligent-AssetLoader               # GLTF::Model — glTF/GLB loading (self-contained; no PBR renderer)
        Diligent-TextureLoader             # CreateTextureFromFile + image decode (PNG/JPG)
        Diligent-Imgui                     # ImGuiImplDiligent renderer backend + vendored Dear ImGui
        DiligentFX                         # post-processing effects (bloom, tone mapping, ...)
        glfw
)

# We link the Vulkan engine as a SHARED library, which Diligent loads at runtime
# via LoadGraphicsEngineVk() (see main.cpp's `#if ENGINE_DLL` path). This define
# selects that dynamic-load path; copy_required_dlls (below) drops the engine
# DLLs next to the executable so the loader finds them.
target_compile_definitions(ToonEngine PRIVATE ENGINE_DLL=1)

# Bake in the absolute path to the HLSL sources so the shader stream factory
# finds them regardless of the working directory (dev convenience). A shipped
# build would copy assets/shaders next to the exe and use a relative path.
target_compile_definitions(ToonEngine PRIVATE
    TOON_SHADERS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders"
    TOON_MODELS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/models"
    TOON_FONTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts"
    TOON_ICON_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png"
    TOON_SCENES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/scenes")

# Convenience function from DiligentCore: copies the engine shared libraries
# next to the executable so the OS can find and load them at runtime.
copy_required_dlls(ToonEngine)