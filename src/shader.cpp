#include "shader.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

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

    prog.id       = id;
    prog.vertPath = vertPath;
    prog.fragPath = fragPath;
    prog.vertTime = fs::last_write_time(vertPath);
    prog.fragTime = fs::last_write_time(fragPath);
    return true;
}

bool ReloadIfChanged(ShaderProgram& prog) {
    std::error_code ec;
    auto vertNow = fs::last_write_time(prog.vertPath, ec);
    if (ec) return false;
    auto fragNow = fs::last_write_time(prog.fragPath, ec);
    if (ec) return false;

    if (vertNow == prog.vertTime && fragNow == prog.fragTime)
        return false;

    // Timestamps changed — update them regardless of compile outcome
    // so we don't spam recompile every frame on a broken shader.
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
