#pragma once
// ImageLoader.h — image file I/O and validation (Member 5 / Member 3)
//
// Uses stb_image for decoding (JPG/PNG, including 16-bit PNG depth maps)
// and stb_image_write for screenshots. No OpenGL in here.

#include "Image.h"

#include <string>

namespace ImageLoader {

// Checks extension (.jpg/.jpeg/.png), existence, decodability and size limits.
bool validateImageFile(const std::string& path, std::string* error, int* width = nullptr,
                       int* height = nullptr);

bool loadRGBA(const std::string& path, ImageRGBA& out, std::string* error);

// Loads a single-channel depth map (16-bit or 8-bit PNG) into [0,1] floats.
bool loadDepth(const std::string& path, DepthImage& out, std::string* error);

bool savePNG(const std::string& path, const ImageRGBA& image);

// False-color preview of a depth map (dark = far, bright = near).
ImageRGBA colorizeDepth(const DepthImage& depth);

// Lowercase file extension including the dot, e.g. ".png".
std::string lowerExtension(const std::string& path);

}  // namespace ImageLoader
