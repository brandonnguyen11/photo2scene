// test_main.cpp — dependency-free unit tests for the non-GL core.
// Run with:  ctest --test-dir build --output-on-failure   (or ./build/p2s_tests)

#include "ImageLoader.h"
#include "MeshData.h"
#include "Reconstruction.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                    \
    } while (0)

static bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

static DepthImage makeDepth(int w, int h, float value) {
    DepthImage d;
    d.width = w;
    d.height = h;
    d.values.assign(static_cast<size_t>(w) * h, value);
    return d;
}

static void testDepthMapping() {
    std::printf("depth mapping\n");
    ReconstructionSettings s;
    s.nearDistance = 1.0f;
    s.farDistance = 4.0f;
    CHECK(near(mapDepthToDistance(1.0f, s), 1.0f));  // 1 = nearest
    CHECK(near(mapDepthToDistance(0.0f, s), 4.0f));  // 0 = farthest
    // Disparity interpolates in 1/z: halfway is 1 / ((1/4 + 1/1) / 2) = 1.6
    CHECK(near(mapDepthToDistance(0.5f, s), 1.6f));
    s.mapping = DepthMapping::Linear;
    CHECK(near(mapDepthToDistance(0.5f, s), 2.5f));
    s.invertDepth = true;
    CHECK(near(mapDepthToDistance(1.0f, s), 4.0f));
    // Monotonic: larger value -> closer.
    s = ReconstructionSettings{};
    float prev = 1e9f;
    for (int i = 0; i <= 10; ++i) {
        const float z = mapDepthToDistance(i / 10.0f, s);
        CHECK(z < prev);
        prev = z;
    }
}

static void testGridSize() {
    std::printf("grid size\n");
    int c = 0, r = 0;
    gridSizeFor(1920, 1080, 384, c, r);
    CHECK(c == 384 && r == 216);
    gridSizeFor(1080, 1920, 384, c, r);
    CHECK(r == 384 && c == 216);
    gridSizeFor(100, 50, 384, c, r);  // never upsample beyond the image
    CHECK(c == 100 && r == 50);
}

static void testFlatDepth() {
    std::printf("flat depth -> planar mesh facing the camera\n");
    ReconstructionSettings s;
    s.gridResolution = 33;
    s.smoothIterations = 0;
    const ReconstructionResult r = reconstructMesh(makeDepth(64, 64, 0.5f), s);
    CHECK(r.ok);
    CHECK(r.stats.gridCols == 33 && r.stats.gridRows == 33);
    CHECK(r.stats.triangleCount == 2u * 32u * 32u);
    CHECK(r.stats.droppedTriangles == 0);
    const float z = mapDepthToDistance(0.5f, s);
    bool allFacing = true, allOnPlane = true;
    for (const Vertex& v : r.mesh.vertices) {
        allFacing &= near(v.normal.z, 1.0f, 1e-3f);
        allOnPlane &= near(v.position.z, -z, 1e-4f);
    }
    CHECK(allFacing);
    CHECK(allOnPlane);
    // The image center projects onto the optical axis.
    bool foundCenter = false;
    for (const Vertex& v : r.mesh.vertices)
        if (near(v.uv.x, 0.5f) && near(v.uv.y, 0.5f))
            foundCenter = near(v.position.x, 0.0f) && near(v.position.y, 0.0f);
    CHECK(foundCenter);
    // Top row of the image (v = 0) is above the bottom row.
    CHECK(r.mesh.boundsMax.y > 0.0f && r.mesh.boundsMin.y < 0.0f);
}

static void testFrustumMatchesFov() {
    std::printf("vertices lie on the camera frustum\n");
    ReconstructionSettings s;
    s.gridResolution = 17;
    s.fovYDeg = 60.0f;
    s.smoothIterations = 0;
    const ReconstructionResult r = reconstructMesh(makeDepth(32, 32, 0.3f), s);
    CHECK(r.ok);
    const float halfH = std::tan(30.0f * 3.14159265f / 180.0f);
    bool ok = true;
    for (const Vertex& v : r.mesh.vertices) {
        const float projectedY = v.position.y / -v.position.z;  // y / z
        const float expected = (1.0f - 2.0f * v.uv.y) * halfH;
        ok &= near(projectedY, expected, 1e-4f);
    }
    CHECK(ok);
}

static void testDiscontinuityCut() {
    std::printf("depth edges are cut, not stretched\n");
    DepthImage d = makeDepth(64, 64, 0.1f);
    for (int y = 0; y < 64; ++y)
        for (int x = 32; x < 64; ++x) d.values[static_cast<size_t>(y) * 64 + x] = 0.95f;

    ReconstructionSettings s;
    s.gridResolution = 64;
    s.smoothIterations = 0;
    s.cutDiscontinuities = true;
    const ReconstructionResult cut = reconstructMesh(d, s);
    CHECK(cut.ok);
    CHECK(cut.stats.droppedTriangles > 0);

    s.cutDiscontinuities = false;
    const ReconstructionResult kept = reconstructMesh(d, s);
    CHECK(kept.ok);
    CHECK(kept.stats.droppedTriangles == 0);
    CHECK(kept.stats.triangleCount > cut.stats.triangleCount);
}

static void testBackgroundClipAndErrors() {
    std::printf("background clip and error handling\n");
    ReconstructionSettings s;
    s.backgroundClip = 0.5f;
    const ReconstructionResult all = reconstructMesh(makeDepth(16, 16, 0.2f), s);
    CHECK(!all.ok);  // everything clipped -> clear error, no crash
    CHECK(!all.error.empty());

    const ReconstructionResult empty = reconstructMesh(DepthImage{}, ReconstructionSettings{});
    CHECK(!empty.ok);

    DepthImage nan = makeDepth(8, 8, std::nanf(""));
    const ReconstructionResult r = reconstructMesh(nan, ReconstructionSettings{});
    CHECK(r.ok);  // invalid values are sanitized
}

static void testBilateralPreservesEdges() {
    std::printf("bilateral smoothing keeps edges\n");
    const int n = 20;
    std::vector<float> grid(n * n, 0.0f);
    for (int y = 0; y < n; ++y)
        for (int x = n / 2; x < n; ++x) grid[y * n + x] = 1.0f;
    grid[5 * n + 3] = 0.02f;  // small noise on the flat side
    bilateralSmooth(grid, n, n, 0.05f, 2);
    CHECK(grid[5 * n + 3] < 0.02f);                  // noise reduced
    CHECK(grid[5 * n + (n / 2 - 1)] < 0.05f);        // left of edge stays low
    CHECK(grid[5 * n + (n / 2)] > 0.95f);            // right of edge stays high
}

static void testImageIO() {
    std::printf("image I/O round trip\n");
    ImageRGBA img;
    img.width = 32;
    img.height = 16;
    img.pixels.resize(32 * 16 * 4);
    for (size_t i = 0; i < img.pixels.size(); i += 4) {
        const unsigned char v = static_cast<unsigned char>((i / 4) % 256);
        img.pixels[i] = img.pixels[i + 1] = img.pixels[i + 2] = v;
        img.pixels[i + 3] = 255;
    }
    const std::string path = (std::filesystem::temp_directory_path() / "p2s_test.png").string();
    CHECK(ImageLoader::savePNG(path, img));

    std::string err;
    int w = 0, h = 0;
    CHECK(ImageLoader::validateImageFile(path, &err, &w, &h));
    CHECK(w == 32 && h == 16);

    DepthImage d;
    CHECK(ImageLoader::loadDepth(path, d, &err));
    CHECK(d.width == 32 && d.height == 16);
    CHECK(near(d.values[5], 5.0f / 255.0f, 1e-3f));

    CHECK(!ImageLoader::validateImageFile("missing_file.png", &err));
    CHECK(!ImageLoader::validateImageFile("notes.txt", &err));
    std::filesystem::remove(path);
}

int main() {
    testDepthMapping();
    testGridSize();
    testFlatDepth();
    testFrustumMatchesFov();
    testDiscontinuityCut();
    testBackgroundClipAndErrors();
    testBilateralPreservesEdges();
    testImageIO();
    std::printf("\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
