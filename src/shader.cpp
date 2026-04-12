#include "shader.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

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

GLuint LoadShaderProgram(const char* vertPath, const char* fragPath) {
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
