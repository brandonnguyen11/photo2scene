#pragma once
// Reconstruction.h — depth map -> textured 3D mesh (Member 4: Geometry)
//
// Model: the photo was taken by a pinhole camera at the origin looking down
// -Z with vertical field of view `fovYDeg`. Each grid sample (u, v) becomes the
// point  Z * ((2u-1) * tan(fov/2) * aspect, (1-2v) * tan(fov/2), -1),
// where Z is the distance recovered from the normalized depth value.
//
// Depth Anything V2 outputs *relative inverse depth* (disparity) with an
// unknown scale and shift, so we map it onto a user-chosen [near, far] range.

#include "Image.h"
#include "MeshData.h"

#include <string>
#include <vector>

enum class DepthMapping : int {
    Disparity = 0,  // value is inverse depth (correct for Depth Anything V2)
    Linear = 1,     // value is linear depth
};

struct ReconstructionSettings {
    int gridResolution = 384;       // vertices along the image's longest side
    float fovYDeg = 50.0f;          // assumed vertical FOV of the original photo
    float nearDistance = 1.0f;      // distance of the nearest point (scene units)
    float farDistance = 2.5f;       // distance of the farthest point
    DepthMapping mapping = DepthMapping::Disparity;
    bool invertDepth = false;       // set if the map uses 0 = near
    bool flatPlane = false;         // Experiment 5 baseline: ignore depth entirely

    int smoothIterations = 1;       // edge-preserving (bilateral) smoothing passes
    float smoothRangeSigma = 0.04f; // bilateral range sigma, in normalized depth units

    bool cutDiscontinuities = true; // drop "rubber-sheet" triangles across depth edges
    float discontinuityThreshold = 0.06f;  // max relative depth spread inside a triangle

    float backgroundClip = 0.0f;    // drop samples with normalized depth below this
};

struct ReconstructionStats {
    int gridCols = 0;
    int gridRows = 0;
    size_t vertexCount = 0;
    size_t triangleCount = 0;
    size_t droppedTriangles = 0;
    double timeMs = 0.0;
};

struct ReconstructionResult {
    bool ok = false;
    std::string error;
    MeshData mesh;
    ReconstructionStats stats;
};

ReconstructionResult reconstructMesh(const DepthImage& depth, const ReconstructionSettings& s);

// --- Helpers exposed for unit tests ---------------------------------------
float mapDepthToDistance(float normalized, const ReconstructionSettings& s);
std::vector<float> resampleBilinear(const DepthImage& depth, int cols, int rows);
void bilateralSmooth(std::vector<float>& grid, int cols, int rows, float rangeSigma,
                     int iterations);
void gridSizeFor(int imageWidth, int imageHeight, int resolution, int& cols, int& rows);
