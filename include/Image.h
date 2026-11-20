#pragma once
// Image.h — plain CPU image containers shared by every module (no OpenGL).

#include <vector>

// 8-bit RGBA image, row 0 = top row of the picture.
struct ImageRGBA {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels;  // width * height * 4

    bool empty() const { return width <= 0 || height <= 0 || pixels.empty(); }
};

// Normalized relative depth, row 0 = top row.
// Convention (see docs/INTERFACES.md): 1.0 = NEAREST, 0.0 = FARTHEST.
// This matches Depth Anything V2, which predicts relative inverse depth.
struct DepthImage {
    int width = 0;
    int height = 0;
    std::vector<float> values;  // width * height, in [0, 1]

    bool empty() const { return width <= 0 || height <= 0 || values.empty(); }
    float at(int x, int y) const { return values[static_cast<size_t>(y) * width + x]; }
};
