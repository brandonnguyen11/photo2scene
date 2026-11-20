#pragma once
// Texture.h — RAII OpenGL 2D texture (Member 1: Graphics Engine)

#include "Image.h"

#include <glad/gl.h>

#include <cstddef>

class Texture2D {
public:
    Texture2D() = default;
    ~Texture2D() { destroy(); }
    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;
    Texture2D(Texture2D&& o) noexcept { *this = std::move(o); }
    Texture2D& operator=(Texture2D&& o) noexcept;

    // Uploads as plain RGBA8. Shaders decode sRGB manually, so the same texture
    // can also be shown unmodified in ImGui previews.
    bool create(const ImageRGBA& image, bool mipmaps = true);
    void destroy();

    GLuint id() const { return id_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool valid() const { return id_ != 0; }
    size_t gpuBytes() const;

private:
    GLuint id_ = 0;
    int width_ = 0;
    int height_ = 0;
    bool mipmaps_ = false;
};
