cmake_minimum_required(VERSION 3.20)

project(ToonEngine CXX)

# ---------------------------------------------------------------------------
#  ToonEngine — minimal Diligent Engine starting point
#  Rendering-only (DiligentCore) + GLFW windowing, single Vulkan backend.
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Diligent Engine (git submodules under external/) ------------------------
# Core is self-contained and implements the D3D11/D3D12/OpenGL/Vulkan backends.
# Uncomment Tools/FX later when you want Dear ImGui, texture/GLTF loading,
# the PBR renderer, or the post-processing effects.
add_subdirectory(external/DiligentCore)
# add_subdirectory(external/DiligentTools)
# add_subdirectory(external/DiligentFX)

# --- GLFW: cross-platform window + input (submodule under external/) ----------
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(external/glfw)

# --- Application -------------------------------------------------------------
add_executable(ToonEngine
    src/main.cpp
    src/core/renderer.cpp
)

# src/ is the include root: cross-directory includes read "core/renderer.h".
target_include_directories(ToonEngine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Diligent-BuildSettings is an INTERFACE target that propagates the platform
# defines (PLATFORM_WIN32 / PLATFORM_LINUX / PLATFORM_MACOS) and compiler flags
# the headers need. Link it first.
target_link_libraries(ToonEngine
    PRIVATE
        Diligent-BuildSettings
        Diligent-GraphicsEngineVk-shared   # Vulkan: Windows, Linux, macOS (via MoltenVK)
        Diligent-Common                    # RefCntAutoPtr.hpp + other Common/interface utilities
        glfw
)

# We link the Vulkan engine as a SHARED library, which Diligent loads at runtime
# via LoadGraphicsEngineVk() (see main.cpp's `#if ENGINE_DLL` path). This define
# selects that dynamic-load path; copy_required_dlls (below) drops the engine
# DLLs next to the executable so the loader finds them.
target_compile_definitions(ToonEngine PRIVATE ENGINE_DLL=1)

# Backend targets only exist on platforms where the backend is available, so
# link the extra ones conditionally. (You don't need them for the Vulkan path,
# but they're handy if you later want D3D12 on Windows, etc.)
if(WIN32)
    target_link_libraries(ToonEngine PRIVATE
        Diligent-GraphicsEngineD3D12-shared
        Diligent-GraphicsEngineD3D11-shared
        Diligent-GraphicsEngineOpenGL-shared)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(ToonEngine PRIVATE
        Diligent-GraphicsEngineOpenGL-shared)
endif()

# Convenience function from DiligentCore: copies the engine shared libraries
# next to the executable so the OS can find and load them at runtime.
copy_required_dlls(ToonEngine)