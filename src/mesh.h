// GPU mesh abstraction over OpenGL VAO/VBO/EBO.
//
// CreateMesh() accepts raw vertex data plus a VertexAttrib array describing
// the interleaved layout (e.g. position, color, texcoord). This keeps vertex
// format knowledge out of the mesh module — callers define their own layouts.

#pragma once

#include <glad/glad.h>

#include <cstddef>
#include <cstdint>

// Describes one vertex attribute within an interleaved vertex buffer.
//   components: number of components (e.g. 3 for vec3)
//   type:       GL data type (e.g. GL_FLOAT)
//   offset:     byte offset from the start of the vertex
struct VertexAttrib {
    GLint  components;
    GLenum type;
    size_t offset;
};

struct Mesh {
    GLuint  vao = 0;
    GLuint  vbo = 0;
    GLuint  ebo = 0;         // 0 when non-indexed
    GLsizei vertexCount = 0; // used by glDrawArrays
    GLsizei indexCount   = 0; // used by glDrawElements (0 = non-indexed)
};

// Upload vertex (and optionally index) data to the GPU and configure
// the vertex attribute pointers described by `attribs`.
Mesh CreateMesh(const void* vertices, size_t verticesSize, GLsizei stride,
                const VertexAttrib* attribs, int attribCount,
                GLsizei vertexCount,
                const std::uint32_t* indices = nullptr, GLsizei indexCount = 0);

void DestroyMesh(Mesh& mesh);

// Bind the VAO and issue the appropriate draw call (indexed or non-indexed).
void DrawMesh(const Mesh& mesh);
