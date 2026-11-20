#include "Texture.h"

#include <utility>

Texture2D& Texture2D::operator=(Texture2D&& o) noexcept {
    if (this != &o) {
        destroy();
        id_ = std::exchange(o.id_, 0);
        width_ = std::exchange(o.width_, 0);
        height_ = std::exchange(o.height_, 0);
        mipmaps_ = o.mipmaps_;
    }
    return *this;
}

bool Texture2D::create(const ImageRGBA& image, bool mipmaps) {
    destroy();
    if (image.empty()) return false;

    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width, image.height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, image.pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    if (mipmaps) glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);

    width_ = image.width;
    height_ = image.height;
    mipmaps_ = mipmaps;
    return true;
}

void Texture2D::destroy() {
    if (id_ != 0) {
        glDeleteTextures(1, &id_);
        id_ = 0;
    }
    width_ = height_ = 0;
}

size_t Texture2D::gpuBytes() const {
    const size_t base = static_cast<size_t>(width_) * height_ * 4;
    return mipmaps_ ? base * 4 / 3 : base;
}
