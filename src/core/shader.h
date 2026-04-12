// Shader program loading, compilation, and hot-reload support.
//
// ShaderProgram tracks vertex/fragment source file paths and their last
// modification timestamps. Call ReloadIfChanged() each frame to detect
// edits on disk and recompile automatically — failed recompiles keep
// the previous working program so the screen never goes black.

#pragma once

#include <glad/glad.h>

#include <filesystem>
#include <string>

struct ShaderProgram {
    GLuint      id = 0;
    std::string vertPath;
    std::string fragPath;
    std::filesystem::file_time_type vertTime{};
    std::filesystem::file_time_type fragTime{};
};

// Compile and link a shader program from vertex/fragment source files.
// Returns false on any compile or link error (logged to stderr).
bool LoadShader(ShaderProgram& prog, const char* vertPath, const char* fragPath);

// Check source file timestamps and recompile if either changed.
// Returns true if a new program was successfully swapped in.
bool ReloadIfChanged(ShaderProgram& prog);

void DestroyShader(ShaderProgram& prog);
