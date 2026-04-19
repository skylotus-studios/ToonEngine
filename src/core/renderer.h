// Renderer abstraction layer.
//
// All OpenGL (glad) calls are confined to renderer.cpp. The rest of the
// engine interacts with opaque handle types and backend-agnostic enums.
// Adding a Vulkan backend means creating a new renderer.cpp that
// implements the same functions — no other engine code changes.

#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>

// ---------------------------------------------------------------------------
// Opaque resource handles (0 = invalid).
// ---------------------------------------------------------------------------

struct MeshHandle        { uint32_t id = 0; explicit operator bool() const { return id != 0; } };
struct ShaderHandle      { uint32_t id = 0; explicit operator bool() const { return id != 0; } };
struct TextureHandle     { uint32_t id = 0; explicit operator bool() const { return id != 0; } };
struct FramebufferHandle { uint32_t id = 0; explicit operator bool() const { return id != 0; } };

// ---------------------------------------------------------------------------
// Enums (replace GL constants in public interfaces).
// ---------------------------------------------------------------------------

enum class AttribType { Float, Int, UByte };

struct VertexAttrib {
    int        components;
    AttribType type;
    size_t     offset;
    int        location = -1;  // -1 = use array index
};

enum class CullMode  { None, Front, Back };
enum class DepthFunc { Less, LEqual, Always, Disabled };
enum class BlendMode { None, Alpha, Additive };

struct RenderState {
    CullMode  cull  = CullMode::Back;
    DepthFunc depth = DepthFunc::Less;
    BlendMode blend = BlendMode::None;
};

enum class FBAttachment { DepthArray, Depth, Color0 };

struct FramebufferDesc {
    int          width  = 0;
    int          height = 0;
    FBAttachment attachment = FBAttachment::Depth;
    int          layers = 1;  // >1 for texture arrays (CSM)
};

// ---------------------------------------------------------------------------
// Lifecycle.
// ---------------------------------------------------------------------------

// Load GL function pointers (via GLAD) and initialize default state.
// Must be called after making the GL context current. Returns false on failure.
bool RendererInit(void* (*glLoadProc)(const char*));
void RendererShutdown();

// ---------------------------------------------------------------------------
// Mesh.
// ---------------------------------------------------------------------------

MeshHandle CreateMesh(const void* vertices, size_t verticesSize, int stride,
                      const VertexAttrib* attribs, int attribCount,
                      int vertexCount,
                      const uint32_t* indices = nullptr, int indexCount = 0);

MeshHandle CreateFullscreenTriangle();

void DestroyMesh(MeshHandle h);
void DrawMesh(MeshHandle h);
int  GetMeshVertexCount(MeshHandle h);
int  GetMeshIndexCount(MeshHandle h);

// ---------------------------------------------------------------------------
// Shader.
// ---------------------------------------------------------------------------

ShaderHandle LoadShader(const char* vertPath, const char* fragPath);
bool         ReloadIfChanged(ShaderHandle h);
void         DestroyShader(ShaderHandle h);

// ---------------------------------------------------------------------------
// Texture.
// ---------------------------------------------------------------------------

TextureHandle CreateWhiteTexture();
TextureHandle CreateCheckerTexture(int size = 256, int cells = 8);
TextureHandle CreateTestNormalMap(int size = 256);
TextureHandle UploadTexture(const unsigned char* pixels, int w, int h);
TextureHandle LoadTexture(const char* path, bool flipY = true);
TextureHandle LoadTextureFromMemory(const unsigned char* data, int dataSize,
                                    bool flipY = true);

int  GetTextureWidth(TextureHandle h);
int  GetTextureHeight(TextureHandle h);
void DestroyTexture(TextureHandle h);

// ---------------------------------------------------------------------------
// Framebuffer.
// ---------------------------------------------------------------------------

FramebufferHandle CreateFramebuffer(const FramebufferDesc& desc);
TextureHandle     GetFramebufferTexture(FramebufferHandle h);
void              SetFramebufferLayer(FramebufferHandle h, int layer);
void              DestroyFramebuffer(FramebufferHandle h);

// ---------------------------------------------------------------------------
// State.
// ---------------------------------------------------------------------------

void SetRenderState(const RenderState& state);
void SetViewport(int x, int y, int w, int h);
void SetClearColor(float r, float g, float b, float a = 1.0f);
void Clear(bool color, bool depth);

// ---------------------------------------------------------------------------
// Bind + Draw.
// ---------------------------------------------------------------------------

void BindShader(ShaderHandle h);
void BindTexture(TextureHandle h, int unit);
void BindTextureArray(TextureHandle h, int unit);
void BindFramebuffer(FramebufferHandle h);  // 0-handle = default/screen

// ---------------------------------------------------------------------------
// Uniforms (apply to the currently-bound shader).
// ---------------------------------------------------------------------------

void SetUniform(const char* name, int value);
void SetUniform(const char* name, float value);
void SetUniform(const char* name, const glm::vec2& value);
void SetUniform(const char* name, const glm::vec3& value);
void SetUniform(const char* name, const glm::vec4& value);
void SetUniform(const char* name, const glm::mat4& value);
void SetUniformArray(const char* name, const glm::mat4* values, int count);
void SetUniformArray(const char* name, const float* values, int count);
