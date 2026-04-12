// Shader compilation pipeline: read source from disk, compile vertex and
// fragment stages separately, link into a GL program. Hot-reload support
// compares file timestamps each frame and attempts recompile on change.

#include "shader.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

// Read an entire text file into a string. Returns empty on failure.
static std::string ReadFile(const char* path) {
    std::ifstream f(path);
    if (!f) {
        std::fprintf(stderr, "Failed to open: %s\n", path);
        return {};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Compile a single shader stage. Returns 0 on failure (error logged).
static GLuint CompileShader(GLenum type, const char* source, const char* label) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[SHADER %s] compile error:\n%s\n", label, log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// Read, compile, and link a vertex + fragment shader into a program.
// Shader objects are deleted after linking (GL keeps internal refs).
static GLuint BuildProgram(const char* vertPath, const char* fragPath) {
    std::string vertSrc = ReadFile(vertPath);
    std::string fragSrc = ReadFile(fragPath);
    if (vertSrc.empty() || fragSrc.empty()) return 0;

    GLuint vert = CompileShader(GL_VERTEX_SHADER, vertSrc.c_str(), vertPath);
    GLuint frag = CompileShader(GL_FRAGMENT_SHADER, fragSrc.c_str(), fragPath);
    if (!vert || !frag) {
        if (vert) glDeleteShader(vert);
        if (frag) glDeleteShader(frag);
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);

    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[SHADER LINK] error:\n%s\n", log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

bool LoadShader(ShaderProgram& prog, const char* vertPath, const char* fragPath) {
    GLuint id = BuildProgram(vertPath, fragPath);
    if (!id) return false;

    // Cache timestamps for hot-reload comparison. Uses error_code overload
    // so a filesystem failure doesn't throw — timestamps stay default.
    std::error_code ec;
    prog.id       = id;
    prog.vertPath = vertPath;
    prog.fragPath = fragPath;
    prog.vertTime = fs::last_write_time(vertPath, ec);
    prog.fragTime = fs::last_write_time(fragPath, ec);
    return true;
}

bool ReloadIfChanged(ShaderProgram& prog) {
    // error_code overload: if a file is inaccessible (e.g. mid-save by
    // the editor), we silently skip rather than throwing.
    std::error_code ec;
    auto vertNow = fs::last_write_time(prog.vertPath, ec);
    if (ec) return false;
    auto fragNow = fs::last_write_time(prog.fragPath, ec);
    if (ec) return false;

    if (vertNow == prog.vertTime && fragNow == prog.fragTime)
        return false;

    // Update timestamps regardless of compile outcome so we don't
    // spam recompile every frame on a broken shader.
    prog.vertTime = vertNow;
    prog.fragTime = fragNow;

    GLuint newId = BuildProgram(prog.vertPath.c_str(), prog.fragPath.c_str());
    if (!newId) {
        std::fprintf(stderr, "[SHADER] Reload failed, keeping previous program\n");
        return false;
    }

    glDeleteProgram(prog.id);
    prog.id = newId;
    std::printf("[SHADER] Reloaded successfully\n");
    return true;
}

void DestroyShader(ShaderProgram& prog) {
    if (prog.id) {
        glDeleteProgram(prog.id);
        prog.id = 0;
    }
}
