// Renderer abstraction — OpenGL 4.1 backend.
//
// This is the ONLY file that includes <glad/glad.h>. All GL calls are here.

#include "renderer.h"

#include <glad/glad.h>

#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Internal storage.
// ---------------------------------------------------------------------------

namespace {

struct MeshSlot {
    GLuint  vao = 0, vbo = 0, ebo = 0;
    GLsizei vertexCount = 0, indexCount = 0;
};

struct ShaderSlot {
    GLuint      program = 0;
    std::string vertPath, fragPath;
    fs::file_time_type vertTime{}, fragTime{};
};

struct TextureSlot {
    GLuint id = 0;
    int    width = 0, height = 0;
};

struct FramebufferSlot {
    GLuint fbo = 0;
    GLuint texture = 0;  // attached texture (owned by this slot)
    int    layers = 1;
};

std::vector<MeshSlot>        gMeshes;
std::vector<ShaderSlot>      gShaders;
std::vector<TextureSlot>     gTextures;
std::vector<FramebufferSlot> gFramebuffers;

GLuint gBoundProgram = 0;

// Alloc helpers — 1-based IDs so 0 stays invalid.
template<typename T>
uint32_t AllocSlot(std::vector<T>& v) {
    v.emplace_back();
    return static_cast<uint32_t>(v.size());  // 1-based
}

template<typename T>
T& GetSlot(std::vector<T>& v, uint32_t id) {
    return v[id - 1];
}

template<typename T>
const T& GetSlot(const std::vector<T>& v, uint32_t id) {
    return v[id - 1];
}

// ---------------------------------------------------------------------------
// GL helpers (internal).
// ---------------------------------------------------------------------------

GLenum AttribToGL(AttribType t) {
    switch (t) {
    case AttribType::Float: return GL_FLOAT;
    case AttribType::Int:   return GL_INT;
    case AttribType::UByte: return GL_UNSIGNED_BYTE;
    }
    return GL_FLOAT;
}

std::string ReadFile(const char* path) {
    std::ifstream f(path);
    if (!f) { std::fprintf(stderr, "Failed to open: %s\n", path); return {}; }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

GLuint CompileStage(GLenum type, const char* source, const char* label) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &source, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[SHADER %s] compile error:\n%s\n", label, log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

GLuint BuildProgram(const char* vertPath, const char* fragPath) {
    std::string vSrc = ReadFile(vertPath);
    std::string fSrc = ReadFile(fragPath);
    if (vSrc.empty() || fSrc.empty()) return 0;

    GLuint v = CompileStage(GL_VERTEX_SHADER,   vSrc.c_str(), vertPath);
    GLuint f = CompileStage(GL_FRAGMENT_SHADER, fSrc.c_str(), fragPath);
    if (!v || !f) {
        if (v) glDeleteShader(v);
        if (f) glDeleteShader(f);
        return 0;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, v);
    glAttachShader(prog, f);
    glLinkProgram(prog);
    glDeleteShader(v);
    glDeleteShader(f);

    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[1024];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[SHADER LINK] error:\n%s\n", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

void UploadTextureRGBA(TextureSlot& slot, const unsigned char* pixels) {
    glGenTextures(1, &slot.id);
    glBindTexture(GL_TEXTURE_2D, slot.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                 slot.width, slot.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

} // namespace

// ---------------------------------------------------------------------------
// Lifecycle.
// ---------------------------------------------------------------------------

bool RendererInit(void* (*glLoadProc)(const char*)) {
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glLoadProc)))
        return false;

    std::printf("GL vendor:   %s\n", reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
    std::printf("GL renderer: %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    std::printf("GL version:  %s\n", reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    std::printf("GLSL:        %s\n", reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION)));

    if (glDebugMessageCallback) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    }

    glEnable(GL_DEPTH_TEST);
    return true;
}

void RendererShutdown() {
    for (auto& m : gMeshes) {
        if (m.ebo) glDeleteBuffers(1, &m.ebo);
        if (m.vbo) glDeleteBuffers(1, &m.vbo);
        if (m.vao) glDeleteVertexArrays(1, &m.vao);
    }
    for (auto& s : gShaders)
        if (s.program) glDeleteProgram(s.program);
    for (auto& t : gTextures)
        if (t.id) glDeleteTextures(1, &t.id);
    for (auto& fb : gFramebuffers) {
        if (fb.fbo) glDeleteFramebuffers(1, &fb.fbo);
        if (fb.texture) glDeleteTextures(1, &fb.texture);
    }
    gMeshes.clear();
    gShaders.clear();
    gTextures.clear();
    gFramebuffers.clear();
}

// ---------------------------------------------------------------------------
// Mesh.
// ---------------------------------------------------------------------------

MeshHandle CreateMesh(const void* vertices, size_t verticesSize, int stride,
                      const VertexAttrib* attribs, int attribCount,
                      int vertexCount,
                      const uint32_t* indices, int indexCount) {
    uint32_t id = AllocSlot(gMeshes);
    MeshSlot& m = GetSlot(gMeshes, id);
    m.vertexCount = static_cast<GLsizei>(vertexCount);
    m.indexCount  = static_cast<GLsizei>(indexCount);

    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glBindVertexArray(m.vao);

    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verticesSize), vertices, GL_STATIC_DRAW);

    for (int i = 0; i < attribCount; ++i) {
        GLuint loc = (attribs[i].location >= 0)
            ? static_cast<GLuint>(attribs[i].location)
            : static_cast<GLuint>(i);
        glVertexAttribPointer(loc, attribs[i].components,
                              AttribToGL(attribs[i].type),
                              GL_FALSE, static_cast<GLsizei>(stride),
                              reinterpret_cast<const void*>(attribs[i].offset));
        glEnableVertexAttribArray(loc);
    }

    if (indices && indexCount > 0) {
        glGenBuffers(1, &m.ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(indexCount * sizeof(uint32_t)),
                     indices, GL_STATIC_DRAW);
    }

    glBindVertexArray(0);
    return {id};
}

MeshHandle CreateFullscreenTriangle() {
    uint32_t id = AllocSlot(gMeshes);
    MeshSlot& m = GetSlot(gMeshes, id);
    m.vertexCount = 3;
    glGenVertexArrays(1, &m.vao);
    return {id};
}

void DestroyMesh(MeshHandle h) {
    if (!h) return;
    MeshSlot& m = GetSlot(gMeshes, h.id);
    if (m.ebo) glDeleteBuffers(1, &m.ebo);
    if (m.vbo) glDeleteBuffers(1, &m.vbo);
    if (m.vao) glDeleteVertexArrays(1, &m.vao);
    m = {};
}

int GetMeshVertexCount(MeshHandle h) {
    if (!h) return 0;
    return GetSlot(gMeshes, h.id).vertexCount;
}

int GetMeshIndexCount(MeshHandle h) {
    if (!h) return 0;
    return GetSlot(gMeshes, h.id).indexCount;
}

void DrawMesh(MeshHandle h) {
    if (!h) return;
    const MeshSlot& m = GetSlot(gMeshes, h.id);
    glBindVertexArray(m.vao);
    if (m.indexCount > 0)
        glDrawElements(GL_TRIANGLES, m.indexCount, GL_UNSIGNED_INT, nullptr);
    else
        glDrawArrays(GL_TRIANGLES, 0, m.vertexCount);
}

// ---------------------------------------------------------------------------
// Shader.
// ---------------------------------------------------------------------------

ShaderHandle LoadShader(const char* vertPath, const char* fragPath) {
    GLuint prog = BuildProgram(vertPath, fragPath);
    if (!prog) return {};

    uint32_t id = AllocSlot(gShaders);
    ShaderSlot& s = GetSlot(gShaders, id);
    s.program  = prog;
    s.vertPath = vertPath;
    s.fragPath = fragPath;
    std::error_code ec;
    s.vertTime = fs::last_write_time(vertPath, ec);
    s.fragTime = fs::last_write_time(fragPath, ec);
    return {id};
}

bool ReloadIfChanged(ShaderHandle h) {
    if (!h) return false;
    ShaderSlot& s = GetSlot(gShaders, h.id);

    std::error_code ec;
    auto vt = fs::last_write_time(s.vertPath, ec);
    if (ec) return false;
    auto ft = fs::last_write_time(s.fragPath, ec);
    if (ec) return false;

    if (vt == s.vertTime && ft == s.fragTime) return false;
    s.vertTime = vt;
    s.fragTime = ft;

    GLuint newProg = BuildProgram(s.vertPath.c_str(), s.fragPath.c_str());
    if (!newProg) {
        std::fprintf(stderr, "[SHADER] Reload failed, keeping previous program\n");
        return false;
    }

    glDeleteProgram(s.program);
    s.program = newProg;
    std::printf("[SHADER] Reloaded successfully\n");
    return true;
}

void DestroyShader(ShaderHandle h) {
    if (!h) return;
    ShaderSlot& s = GetSlot(gShaders, h.id);
    if (s.program) glDeleteProgram(s.program);
    s = {};
}

// ---------------------------------------------------------------------------
// Texture.
// ---------------------------------------------------------------------------

TextureHandle CreateWhiteTexture() {
    uint32_t id = AllocSlot(gTextures);
    TextureSlot& t = GetSlot(gTextures, id);
    t.width = 1; t.height = 1;

    unsigned char pixel[] = {255, 255, 255, 255};
    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return {id};
}

TextureHandle CreateCheckerTexture(int size, int cells) {
    uint32_t id = AllocSlot(gTextures);
    TextureSlot& t = GetSlot(gTextures, id);
    t.width = size; t.height = size;

    std::vector<unsigned char> pixels(size * size * 4);
    int cellSize = size / cells;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            bool white = ((x / cellSize) + (y / cellSize)) % 2 == 0;
            unsigned char c = white ? 220 : 50;
            int i = (y * size + x) * 4;
            pixels[i] = c; pixels[i+1] = c; pixels[i+2] = c; pixels[i+3] = 255;
        }

    UploadTextureRGBA(t, pixels.data());
    return {id};
}

TextureHandle CreateTestNormalMap(int size) {
    uint32_t id = AllocSlot(gTextures);
    TextureSlot& t = GetSlot(gTextures, id);
    t.width = size; t.height = size;

    std::vector<unsigned char> pixels(size * size * 4);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            float u = static_cast<float>(x) / size;
            float v = static_cast<float>(y) / size;
            float freq = 6.2832f * 4.0f;
            float nx = std::cos(u * freq) * 0.4f;
            float ny = std::cos(v * freq) * 0.4f;
            float nz = 1.0f;
            float len = std::sqrt(nx*nx + ny*ny + nz*nz);
            nx /= len; ny /= len; nz /= len;
            int i = (y * size + x) * 4;
            pixels[i]   = static_cast<unsigned char>((nx * 0.5f + 0.5f) * 255.0f);
            pixels[i+1] = static_cast<unsigned char>((ny * 0.5f + 0.5f) * 255.0f);
            pixels[i+2] = static_cast<unsigned char>((nz * 0.5f + 0.5f) * 255.0f);
            pixels[i+3] = 255;
        }

    UploadTextureRGBA(t, pixels.data());
    return {id};
}

TextureHandle UploadTexture(const unsigned char* pixels, int w, int h) {
    uint32_t id = AllocSlot(gTextures);
    TextureSlot& t = GetSlot(gTextures, id);
    t.width = w; t.height = h;
    UploadTextureRGBA(t, pixels);
    return {id};
}

TextureHandle LoadTexture(const char* path, bool flipY) {
    stbi_set_flip_vertically_on_load(flipY ? 1 : 0);
    int w = 0, h = 0, ch = 0;
    unsigned char* pixels = stbi_load(path, &w, &h, &ch, 4);
    if (!pixels) {
        std::fprintf(stderr, "Failed to load texture: %s\n", path);
        return {};
    }
    uint32_t id = AllocSlot(gTextures);
    TextureSlot& t = GetSlot(gTextures, id);
    t.width = w; t.height = h;
    UploadTextureRGBA(t, pixels);
    stbi_image_free(pixels);
    return {id};
}

TextureHandle LoadTextureFromMemory(const unsigned char* data, int dataSize,
                                    bool flipY) {
    stbi_set_flip_vertically_on_load(flipY ? 1 : 0);
    int w = 0, h = 0, ch = 0;
    unsigned char* pixels = stbi_load_from_memory(data, dataSize, &w, &h, &ch, 4);
    if (!pixels) {
        std::fprintf(stderr, "Failed to decode texture from memory\n");
        return {};
    }
    uint32_t id = AllocSlot(gTextures);
    TextureSlot& t = GetSlot(gTextures, id);
    t.width = w; t.height = h;
    UploadTextureRGBA(t, pixels);
    stbi_image_free(pixels);
    return {id};
}

int GetTextureWidth(TextureHandle h) {
    if (!h) return 0;
    return GetSlot(gTextures, h.id).width;
}

int GetTextureHeight(TextureHandle h) {
    if (!h) return 0;
    return GetSlot(gTextures, h.id).height;
}

void DestroyTexture(TextureHandle h) {
    if (!h) return;
    TextureSlot& t = GetSlot(gTextures, h.id);
    if (t.id) glDeleteTextures(1, &t.id);
    t = {};
}

// ---------------------------------------------------------------------------
// Framebuffer.
// ---------------------------------------------------------------------------

FramebufferHandle CreateFramebuffer(const FramebufferDesc& desc) {
    uint32_t id = AllocSlot(gFramebuffers);
    FramebufferSlot& fb = GetSlot(gFramebuffers, id);
    fb.layers = desc.layers;

    glGenTextures(1, &fb.texture);

    if (desc.attachment == FBAttachment::DepthArray) {
        glBindTexture(GL_TEXTURE_2D_ARRAY, fb.texture);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F,
                     desc.width, desc.height, desc.layers,
                     0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float border[] = {1, 1, 1, 1};
        glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, border);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

        glGenFramebuffers(1, &fb.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  fb.texture, 0, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    } else if (desc.attachment == FBAttachment::Depth) {
        glBindTexture(GL_TEXTURE_2D, fb.texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F,
                     desc.width, desc.height, 0,
                     GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        glGenFramebuffers(1, &fb.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                               GL_TEXTURE_2D, fb.texture, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    } else {
        glBindTexture(GL_TEXTURE_2D, fb.texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                     desc.width, desc.height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glGenFramebuffers(1, &fb.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, fb.texture, 0);
    }

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::fprintf(stderr, "FBO incomplete (%dx%d)\n", desc.width, desc.height);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return {id};
}

TextureHandle GetFramebufferTexture(FramebufferHandle h) {
    if (!h) return {};
    const FramebufferSlot& fb = GetSlot(gFramebuffers, h.id);
    // Return a handle to a "virtual" texture slot that wraps the FBO's texture.
    // Allocate a slot that borrows the existing GLuint.
    uint32_t tid = AllocSlot(gTextures);
    TextureSlot& t = GetSlot(gTextures, tid);
    t.id = fb.texture;
    t.width = 0; t.height = 0;
    return {tid};
}

void SetFramebufferLayer(FramebufferHandle h, int layer) {
    if (!h) return;
    const FramebufferSlot& fb = GetSlot(gFramebuffers, h.id);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              fb.texture, 0, layer);
}

void DestroyFramebuffer(FramebufferHandle h) {
    if (!h) return;
    FramebufferSlot& fb = GetSlot(gFramebuffers, h.id);
    if (fb.fbo) glDeleteFramebuffers(1, &fb.fbo);
    if (fb.texture) glDeleteTextures(1, &fb.texture);
    fb = {};
}

// ---------------------------------------------------------------------------
// State.
// ---------------------------------------------------------------------------

void SetRenderState(const RenderState& state) {
    // Cull.
    if (state.cull == CullMode::None) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
        glCullFace(state.cull == CullMode::Front ? GL_FRONT : GL_BACK);
    }

    // Depth.
    if (state.depth == DepthFunc::Disabled) {
        glDisable(GL_DEPTH_TEST);
    } else {
        glEnable(GL_DEPTH_TEST);
        switch (state.depth) {
        case DepthFunc::Less:   glDepthFunc(GL_LESS);    break;
        case DepthFunc::LEqual: glDepthFunc(GL_LEQUAL);  break;
        case DepthFunc::Always: glDepthFunc(GL_ALWAYS);  break;
        default: break;
        }
    }

    // Blend.
    if (state.blend == BlendMode::None) {
        glDisable(GL_BLEND);
    } else {
        glEnable(GL_BLEND);
        if (state.blend == BlendMode::Alpha)
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        else
            glBlendFunc(GL_ONE, GL_ONE);
    }
}

void SetViewport(int x, int y, int w, int h) {
    glViewport(x, y, w, h);
}

void SetClearColor(float r, float g, float b, float a) {
    glClearColor(r, g, b, a);
}

void Clear(bool color, bool depth) {
    GLbitfield bits = 0;
    if (color) bits |= GL_COLOR_BUFFER_BIT;
    if (depth) bits |= GL_DEPTH_BUFFER_BIT;
    if (bits) glClear(bits);
}

// ---------------------------------------------------------------------------
// Bind + Draw.
// ---------------------------------------------------------------------------

void BindShader(ShaderHandle h) {
    if (!h) { glUseProgram(0); gBoundProgram = 0; return; }
    const ShaderSlot& s = GetSlot(gShaders, h.id);
    glUseProgram(s.program);
    gBoundProgram = s.program;
}

void BindTexture(TextureHandle h, int unit) {
    glActiveTexture(GL_TEXTURE0 + unit);
    if (!h) { glBindTexture(GL_TEXTURE_2D, 0); return; }
    glBindTexture(GL_TEXTURE_2D, GetSlot(gTextures, h.id).id);
}

void BindTextureArray(TextureHandle h, int unit) {
    glActiveTexture(GL_TEXTURE0 + unit);
    if (!h) { glBindTexture(GL_TEXTURE_2D_ARRAY, 0); return; }
    glBindTexture(GL_TEXTURE_2D_ARRAY, GetSlot(gTextures, h.id).id);
}

void BindFramebuffer(FramebufferHandle h) {
    if (!h) { glBindFramebuffer(GL_FRAMEBUFFER, 0); return; }
    glBindFramebuffer(GL_FRAMEBUFFER, GetSlot(gFramebuffers, h.id).fbo);
}

// ---------------------------------------------------------------------------
// Uniforms.
// ---------------------------------------------------------------------------

void SetUniform(const char* name, int value) {
    glUniform1i(glGetUniformLocation(gBoundProgram, name), value);
}

void SetUniform(const char* name, float value) {
    glUniform1f(glGetUniformLocation(gBoundProgram, name), value);
}

void SetUniform(const char* name, const glm::vec2& value) {
    glUniform2fv(glGetUniformLocation(gBoundProgram, name), 1, glm::value_ptr(value));
}

void SetUniform(const char* name, const glm::vec3& value) {
    glUniform3fv(glGetUniformLocation(gBoundProgram, name), 1, glm::value_ptr(value));
}

void SetUniform(const char* name, const glm::vec4& value) {
    glUniform4fv(glGetUniformLocation(gBoundProgram, name), 1, glm::value_ptr(value));
}

void SetUniform(const char* name, const glm::mat4& value) {
    glUniformMatrix4fv(glGetUniformLocation(gBoundProgram, name),
                       1, GL_FALSE, glm::value_ptr(value));
}

void SetUniformArray(const char* name, const glm::mat4* values, int count) {
    glUniformMatrix4fv(glGetUniformLocation(gBoundProgram, name),
                       count, GL_FALSE, glm::value_ptr(values[0]));
}

void SetUniformArray(const char* name, const float* values, int count) {
    glUniform1fv(glGetUniformLocation(gBoundProgram, name), count, values);
}
