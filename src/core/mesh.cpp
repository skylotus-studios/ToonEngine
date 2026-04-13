// Mesh GPU resource management: creates VAO/VBO/EBO from raw vertex data,
// configures vertex attribute pointers from a caller-supplied layout, and
// dispatches the correct draw call (indexed vs non-indexed).

#include "mesh.h"

Mesh CreateMesh(const void* vertices, size_t verticesSize, GLsizei stride,
                const VertexAttrib* attribs, int attribCount,
                GLsizei vertexCount,
                const std::uint32_t* indices, GLsizei indexCount) {
    Mesh m{};
    m.vertexCount = vertexCount;
    m.indexCount   = indexCount;

    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glBindVertexArray(m.vao);

    // Upload vertex data.
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verticesSize), vertices, GL_STATIC_DRAW);

    // Configure each attribute. Uses explicit location if set, else array index.
    for (GLuint i = 0; i < static_cast<GLuint>(attribCount); ++i) {
        GLuint loc = (attribs[i].location >= 0)
            ? static_cast<GLuint>(attribs[i].location) : i;
        glVertexAttribPointer(loc, attribs[i].components, attribs[i].type,
                              GL_FALSE, stride,
                              reinterpret_cast<const void*>(attribs[i].offset));
        glEnableVertexAttribArray(loc);
    }

    // Optional index buffer — stays bound to this VAO.
    if (indices && indexCount > 0) {
        glGenBuffers(1, &m.ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(indexCount * sizeof(std::uint32_t)),
                     indices, GL_STATIC_DRAW);
    }

    glBindVertexArray(0);
    return m;
}

void DestroyMesh(Mesh& mesh) {
    if (mesh.ebo) glDeleteBuffers(1, &mesh.ebo);
    if (mesh.vbo) glDeleteBuffers(1, &mesh.vbo);
    if (mesh.vao) glDeleteVertexArrays(1, &mesh.vao);
    mesh = {};
}

void DrawMesh(const Mesh& mesh) {
    glBindVertexArray(mesh.vao);
    if (mesh.indexCount > 0)
        glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
    else
        glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
}
