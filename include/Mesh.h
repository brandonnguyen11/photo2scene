#pragma once
// Mesh.h — GPU mesh (VAO + VBO + EBO) (Member 1: Graphics Engine)
//
// Vertex layout: location 0 = position (vec3), 1 = normal (vec3), 2 = uv (vec2).

#include "MeshData.h"

#include <glad/gl.h>

#include <cstddef>

class Mesh {
public:
    Mesh() = default;
    ~Mesh() { destroy(); }
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& o) noexcept { *this = std::move(o); }
    Mesh& operator=(Mesh&& o) noexcept;

    bool upload(const MeshData& data);
    void draw() const;
    void destroy();

    bool valid() const { return vao_ != 0 && indexCount_ > 0; }
    GLsizei indexCount() const { return indexCount_; }
    size_t gpuBytes() const { return gpuBytes_; }

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    GLsizei indexCount_ = 0;
    size_t gpuBytes_ = 0;
};
