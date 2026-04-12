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

bool LoadShader(ShaderProgram& prog, const char* vertPath, const char* fragPath);
bool ReloadIfChanged(ShaderProgram& prog);
void DestroyShader(ShaderProgram& prog);
