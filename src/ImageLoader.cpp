#include "ImageLoader.h"

// UTF-8 file names on Windows.
#define STBI_WINDOWS_UTF8
#define STBIW_WINDOWS_UTF8
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>

namespace {
constexpr int kMinDimension = 16;
constexpr int kMaxDimension = 16384;
}  // namespace

namespace ImageLoader {

std::string lowerExtension(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

bool validateImageFile(const std::string& path, std::string* error, int* width, int* height) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };
    if (path.empty()) return fail("No file selected.");

    const std::string ext = lowerExtension(path);
    if (ext != ".jpg" && ext != ".jpeg" && ext != ".png")
        return fail("Unsupported format '" + ext + "'. Use JPG, JPEG or PNG.");

    std::error_code ec;
    if (!std::filesystem::is_regular_file(std::filesystem::u8path(path), ec))
        return fail("File not found: " + path);

    int w = 0, h = 0, c = 0;
    if (!stbi_info(path.c_str(), &w, &h, &c))
        return fail(std::string("File is not a readable image (") + stbi_failure_reason() + ").");
    if (w < kMinDimension || h < kMinDimension)
        return fail("Image is too small (" + std::to_string(w) + "x" + std::to_string(h) +
                    "). Minimum is 16x16.");
    if (w > kMaxDimension || h > kMaxDimension)
        return fail("Image is too large (" + std::to_string(w) + "x" + std::to_string(h) +
                    "). Maximum is 16384 on a side.");

    if (width) *width = w;
    if (height) *height = h;
    return true;
}

bool loadRGBA(const std::string& path, ImageRGBA& out, std::string* error) {
    int w = 0, h = 0, c = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &c, 4);
    if (!data) {
        if (error) *error = std::string("Failed to decode image: ") + stbi_failure_reason();
        return false;
    }
    out.width = w;
    out.height = h;
    out.pixels.assign(data, data + static_cast<size_t>(w) * h * 4);
    stbi_image_free(data);
    return true;
}

bool loadDepth(const std::string& path, DepthImage& out, std::string* error) {
    int w = 0, h = 0, c = 0;
    out.values.clear();
    if (stbi_is_16_bit(path.c_str())) {
        stbi_us* data = stbi_load_16(path.c_str(), &w, &h, &c, 1);
        if (!data) {
            if (error) *error = std::string("Failed to decode depth map: ") + stbi_failure_reason();
            return false;
        }
        out.values.resize(static_cast<size_t>(w) * h);
        for (size_t i = 0; i < out.values.size(); ++i) out.values[i] = data[i] / 65535.0f;
        stbi_image_free(data);
    } else {
        unsigned char* data = stbi_load(path.c_str(), &w, &h, &c, 1);
        if (!data) {
            if (error) *error = std::string("Failed to decode depth map: ") + stbi_failure_reason();
            return false;
        }
        out.values.resize(static_cast<size_t>(w) * h);
        for (size_t i = 0; i < out.values.size(); ++i) out.values[i] = data[i] / 255.0f;
        stbi_image_free(data);
    }
    out.width = w;
    out.height = h;
    // Guard against garbage values (requirement 3.3: handle invalid depth).
    for (float& v : out.values) v = std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f;
    return true;
}

bool savePNG(const std::string& path, const ImageRGBA& image) {
    if (image.empty()) return false;
    return stbi_write_png(path.c_str(), image.width, image.height, 4, image.pixels.data(),
                          image.width * 4) != 0;
}

ImageRGBA colorizeDepth(const DepthImage& depth) {
    // Inferno-like 5-stop gradient.
    static const std::array<std::array<float, 3>, 5> stops = {{
        {0.0f, 0.0f, 4.0f},
        {87.0f, 16.0f, 110.0f},
        {188.0f, 55.0f, 84.0f},
        {249.0f, 142.0f, 9.0f},
        {252.0f, 255.0f, 164.0f},
    }};
    ImageRGBA img;
    img.width = depth.width;
    img.height = depth.height;
    img.pixels.resize(static_cast<size_t>(depth.width) * depth.height * 4);
    for (size_t i = 0; i < depth.values.size(); ++i) {
        const float t = std::clamp(depth.values[i], 0.0f, 1.0f) * 4.0f;
        const int k = std::min(static_cast<int>(t), 3);
        const float f = t - static_cast<float>(k);
        for (int ch = 0; ch < 3; ++ch) {
            const float v = stops[k][ch] + (stops[k + 1][ch] - stops[k][ch]) * f;
            img.pixels[i * 4 + ch] = static_cast<unsigned char>(std::clamp(v, 0.0f, 255.0f));
        }
        img.pixels[i * 4 + 3] = 255;
    }
    return img;
}

}  // namespace ImageLoader
