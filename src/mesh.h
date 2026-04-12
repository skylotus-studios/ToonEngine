#pragma once

#include <glad/glad.h>

#include <cstddef>
#include <cstdint>

struct VertexAttrib {
    GLint  components;
    GLenum type;
    size_t offset;
};

struct Mesh {
    GLuint  vao = 0;
    GLuint  vbo = 0;
    GLuint  ebo = 0;
    GLsizei vertexCount = 0;
    GLsizei indexCount   = 0;
};

Mesh CreateMesh(const void* vertices, size_t verticesSize, GLsizei stride,
                const VertexAttrib* attribs, int attribCount,
                GLsizei vertexCount,
                const std::uint32_t* indices = nullptr, GLsizei indexCount = 0);

void DestroyMesh(Mesh& mesh);
void DrawMesh(const Mesh& mesh);
