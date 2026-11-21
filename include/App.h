#pragma once
// App.h — application shell and integration (Member 5: UI, Integration, Testing)
//
// Owns the window, the main loop and every subsystem, and wires the pipeline:
//   upload -> validate -> Python depth (async) -> reconstruction -> render.

#include "Camera.h"
#include "DepthEstimator.h"
#include "Image.h"
#include "Lighting.h"
#include "Reconstruction.h"
#include "Renderer.h"
#include "Scene.h"
#include "Texture.h"
#include "World.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <filesystem>
#include <string>
#include <vector>

struct AppPaths {
    std::filesystem::path root;
    std::filesystem::path shaders;
    std::filesystem::path aiScript;
    std::filesystem::path cacheDir;
    std::filesystem::path outputDir;
};

enum class StatusLevel { Info, Busy, Success, Error };

struct AppOptions {
    int width = 1600;
    int height = 900;
    std::string imagePath;       // optional image to load on start
    std::string depthPath;       // optional precomputed depth map
    std::string screenshotPath;  // if set: render a few frames, save, exit (CI / smoke test)
    std::string worldId;         // royal_garden | beach | forest
    bool dummyDepth = false;     // use synthetic depth instead of the model
    float yawDeg = 0.0f;         // initial camera orbit (for --screenshot comparisons)
    float pitchDeg = 0.0f;
    bool exportOriginalView = false;  // with --screenshot: also export PSNR/SSIM renders
};

class App {
public:
    bool init(const AppOptions& options);
    void run();
    void shutdown();

    void onFileDrop(int count, const char** paths);

private:
    // ---- Pipeline ----
    void loadDemoScene();
    bool loadWorld(const std::string& worldId);
    void frameWorldView();
    bool loadImage(const std::string& path);
    void startDepthEstimation();
    void pollDepthJob();
    bool loadDepthFile(const std::string& path);
    void applyDepth(DepthImage depth);
    void showFlatPlaceholder();
    void rebuildMesh();
    void frameOriginalView();
    void resetAll();

    // ---- Frame ----
    void handleInput(float dt);
    void drawUI();
    void drawControlPanel();
    void drawPreviewPanel();
    void drawImageSection();
    void drawReconstructionSection();
    void drawCameraSection();
    void drawLightingSection();
    void drawEnvironmentSection();
    void drawWorldSection();
    void drawPerformanceSection();

    // ---- Tools (experiments) ----
    void saveScreenshot(int fbWidth, int fbHeight, const std::string& path);
    void exportOriginalViewRenders();
    void updateBenchmark(double frameMs);
    std::string timestamp() const;
    void setStatus(StatusLevel level, const std::string& text);

    GLFWwindow* window_ = nullptr;
    AppOptions options_;
    AppPaths paths_;

    Camera camera_;
    Scene scene_;
    World world_;
    Renderer renderer_;
    LightingSystem lighting_;
    DepthEstimator depthEstimator_;
    bool immersiveMode_ = true;
    std::string currentWorldId_ = "royal_garden";
    bool showPhotoInWorld_ = false;

    // Inputs
    std::string photoName_ = "demo";
    std::string currentImagePath_;   // validated photo on disk ("" for the demo)
    std::string imagePathInput_;
    std::string depthPathInput_;
    ImageRGBA photo_;     // texture source (Python-preprocessed when available)
    DepthImage depth_;
    bool autoEstimate_ = true;

    // GPU textures
    Texture2D photoTexture_;
    Texture2D originalPreview_;
    Texture2D depthPreview_;

    // Reconstruction
    ReconstructionSettings recon_;
    ReconstructionStats reconStats_;
    bool rebuildRequested_ = false;

    // Status
    StatusLevel statusLevel_ = StatusLevel::Info;
    std::string statusText_ = "Ready.";
    std::string lastLog_;

    // Performance
    double cpuFrameMs_ = 0.0;
    double depthSeconds_ = 0.0;
    bool vsync_ = true;
    bool showUI_ = true;
    bool screenshotRequested_ = false;
    struct Benchmark {
        bool running = false;
        int targetFrames = 300;
        std::vector<double> cpuMs;
        std::vector<double> gpuMs;
        std::string summary;
    } bench_;
};
