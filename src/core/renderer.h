//============================================================================
//  core/renderer.h — ToonEngine's rendering seam.
//
//  This is the ONE header the rest of the engine talks to for GPU work. Every
//  backend detail (currently Diligent Engine on Vulkan) lives behind it in
//  renderer.cpp via PIMPL, so no Diligent type — and no Diligent header —
//  escapes into engine or game code. Swapping the backend (or porting to a
//  console) becomes "write another renderer_*.cpp", not a rewrite.
//============================================================================
#pragma once

#include <cstdint>

// Forward-declared so this header pulls in neither GLFW nor any Diligent header.
struct GLFWwindow;

namespace toon {

// --- Opaque GPU resource handles -------------------------------------------
// The engine's vocabulary for GPU resources: plain 32-bit ids (0 is always the
// null/invalid handle). The mapping from id to the underlying backend resource
// lives entirely inside the renderer. Resource-creation APIs land with the
// shader/pipeline work on the roadmap; the types are defined now so the seam's
// contract — "the engine names resources, never Diligent objects" — is explicit.
enum class TextureHandle  : uint32_t { Invalid = 0 };
enum class BufferHandle   : uint32_t { Invalid = 0 };
enum class ShaderHandle   : uint32_t { Invalid = 0 };
enum class PipelineHandle : uint32_t { Invalid = 0 };

struct Color { float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f; };

// --- Renderer ---------------------------------------------------------------
// Owns the graphics device, immediate context, and swap chain, and drives the
// per-frame lifecycle. Backend-agnostic by construction (see file header).
class Renderer {
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Create the device + swap chain for `window` (a GLFW window created with
    // the GLFW_NO_API hint). Returns false on failure (details go to stderr).
    bool Init(GLFWwindow* window);
    void Shutdown();

    // Per-frame: bind the back buffer and clear it, then present.
    void BeginFrame(const Color& clearColor);
    void EndFrame();

    // Keep the swap chain matched to the window's framebuffer size.
    void Resize(uint32_t width, uint32_t height);

private:
    struct Impl;         // defined in renderer.cpp — hides all Diligent types
    Impl* m_impl = nullptr;
};

} // namespace toon
