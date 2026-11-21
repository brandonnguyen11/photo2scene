#include "Mesh.h"

#include <cstddef>
#include <utility>

Mesh& Mesh::operator=(Mesh&& o) noexcept {
    if (this != &o) {
        destroy();
        vao_ = std::exchange(o.vao_, 0);
        vbo_ = std::exchange(o.vbo_, 0);
        ebo_ = std::exchange(o.ebo_, 0);
        instanceVbo_ = std::exchange(o.instanceVbo_, 0);
        indexCount_ = std::exchange(o.indexCount_, 0);
        instanceCount_ = std::exchange(o.instanceCount_, 0);
        gpuBytes_ = std::exchange(o.gpuBytes_, 0);
    }
    return *this;
}

bool Mesh::upload(const MeshData& data) {
    destroy();
    if (data.empty()) return false;

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.vertices.size() * sizeof(Vertex)),
                 data.vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(data.indices.size() * sizeof(uint32_t)),
                 data.indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, uv)));

    glBindVertexArray(0);

    indexCount_ = static_cast<GLsizei>(data.indices.size());
    gpuBytes_ = data.vertices.size() * sizeof(Vertex) + data.indices.size() * sizeof(uint32_t);
    return true;
}

void Mesh::setInstances(const void* data, size_t strideBytes, size_t count) {
    if (!vao_) return;
    if (!instanceVbo_) glGenBuffers(1, &instanceVbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(strideBytes * count), data, GL_DYNAMIC_DRAW);

    // layout: vec4 posScale (loc 3), float yaw (loc 4), float phase (loc 5)
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(strideBytes),
                          reinterpret_cast<void*>(0));
    glVertexAttribDivisor(3, 1);

    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(strideBytes),
                          reinterpret_cast<void*>(sizeof(float) * 4));
    glVertexAttribDivisor(4, 1);

    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(strideBytes),
                          reinterpret_cast<void*>(sizeof(float) * 5));
    glVertexAttribDivisor(5, 1);

    glBindVertexArray(0);
    instanceCount_ = static_cast<GLsizei>(count);
    gpuBytes_ += strideBytes * count;
}

void Mesh::draw() const {
    if (!valid()) return;
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void Mesh::drawInstanced() const {
    if (!valid() || instanceCount_ <= 0) return;
    glBindVertexArray(vao_);
    glDrawElementsInstanced(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr, instanceCount_);
    glBindVertexArray(0);
}

void Mesh::destroy() {
    if (instanceVbo_) glDeleteBuffers(1, &instanceVbo_);
    if (ebo_) glDeleteBuffers(1, &ebo_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    vao_ = vbo_ = ebo_ = instanceVbo_ = 0;
    indexCount_ = instanceCount_ = 0;
    gpuBytes_ = 0;
}
