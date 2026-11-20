#include "App.h"

#include "ImageLoader.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>
#include <numeric>

namespace fs = std::filesystem;

#ifndef P2S_SOURCE_DIR
#define P2S_SOURCE_DIR "."
#endif

namespace {

// Finds the folder that contains shaders/ (works from the build dir or the repo root).
fs::path findProjectRoot() {
    const fs::path cwd = fs::current_path();
    const fs::path candidates[] = {cwd, cwd / "..", cwd / ".." / "..", fs::u8path(P2S_SOURCE_DIR)};
    for (const fs::path& c : candidates) {
        std::error_code ec;
        if (fs::exists(c / "shaders" / "fragment.glsl", ec)) return fs::weakly_canonical(c, ec);
    }
    return cwd;
}

void glfwErrorCallback(int code, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, description);
}

// Procedural demo input so the renderer and lighting can be developed before the
// AI pipeline exists (Weeks 1-4). Flat colors with no baked shading.
void makeDemoInputs(ImageRGBA& rgb, DepthImage& depth) {
    const int w = 640, h = 440;
    rgb.width = depth.width = w;
    rgb.height = depth.height = h;
    rgb.pixels.assign(static_cast<size_t>(w) * h * 4, 255);
    depth.values.assign(static_cast<size_t>(w) * h, 0.0f);

    const float sphereCx = 0.36f * w, sphereCy = 0.52f * h, sphereR = 0.24f * h;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t k = static_cast<size_t>(y) * w + x;
            const float fy = static_cast<float>(y) / h;
            // Background wall (far) with a floor that comes closer toward the bottom.
            float d = fy < 0.7f ? 0.12f : 0.12f + (fy - 0.7f) / 0.3f * 0.55f;
            unsigned char r = 70, g = 110, b = 150;
            if (fy >= 0.7f) {
                const bool tile = (((x / 40) + (y / 20)) & 1) != 0;
                r = tile ? 190 : 160;
                g = tile ? 170 : 140;
                b = tile ? 140 : 115;
            } else if ((x / 16) % 2 == 0) {
                r = 80; g = 120; b = 160;
            }
            // Box on the right (flat front face, medium depth).
            if (x > 0.62f * w && x < 0.86f * w && y > 0.32f * h && y < 0.78f * h) {
                d = 0.55f;
                const bool stripe = ((y / 12) % 2) == 0;
                r = stripe ? 220 : 200; g = stripe ? 120 : 95; b = 60;
            }
            // Sphere (closest), depth follows the sphere's surface.
            const float dx = (x - sphereCx) / sphereR, dy = (y - sphereCy) / sphereR;
            const float rr = dx * dx + dy * dy;
            if (rr < 1.0f) {
                d = 0.7f + 0.28f * std::sqrt(1.0f - rr);
                const bool check = ((static_cast<int>((dx + 1) * 4) + static_cast<int>((dy + 1) * 4)) & 1) != 0;
                r = check ? 235 : 90; g = check ? 235 : 190; b = check ? 235 : 120;
            }
            depth.values[k] = d;
            rgb.pixels[k * 4 + 0] = r;
            rgb.pixels[k * 4 + 1] = g;
            rgb.pixels[k * 4 + 2] = b;
        }
    }
}

}  // namespace

// ===========================================================================
// Lifecycle
// ===========================================================================
bool App::init(const AppOptions& options) {
    options_ = options;

    paths_.root = findProjectRoot();
    paths_.shaders = paths_.root / "shaders";
    paths_.aiScript = paths_.root / "ai" / "depth_estimation.py";
    paths_.cacheDir = paths_.root / "cache";
    paths_.outputDir = paths_.root / "outputs";
    std::error_code ec;
    fs::create_directories(paths_.cacheDir, ec);
    fs::create_directories(paths_.outputDir, ec);

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialize GLFW\n");
        return false;
    }

    // OpenGL 4.1 core: the highest version macOS supports, also fine on Windows/Linux.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);  // required on macOS
    glfwWindowHint(GLFW_SAMPLES, 4);

    window_ = glfwCreateWindow(options.width, options.height, "Photo2Scene", nullptr, nullptr);
    if (!window_) {
        std::fprintf(stderr, "Failed to create an OpenGL 4.1 core window. Update your GPU driver.\n");
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(vsync_ ? 1 : 0);

    if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress))) {
        std::fprintf(stderr, "Failed to load OpenGL functions (GLAD)\n");
        return false;
    }
    std::printf("OpenGL %s | %s\n", reinterpret_cast<const char*>(glGetString(GL_VERSION)),
                reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    std::printf("Project root: %s\n", paths_.root.u8string().c_str());

    glfwSetWindowUserPointer(window_, this);
    glfwSetDropCallback(window_, [](GLFWwindow* w, int count, const char** paths) {
        static_cast<App*>(glfwGetWindowUserPointer(w))->onFileDrop(count, paths);
    });

    // Dear ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;  // always start from the default layout (predictable demos)
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 6.0f;
    ImGui::GetStyle().FrameRounding = 4.0f;
    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 410");

    glEnable(GL_MULTISAMPLE);

    std::string error;
    if (!renderer_.init(paths_.shaders.u8string(), &error)) {
        std::fprintf(stderr, "Renderer init failed:\n%s\n", error.c_str());
        return false;
    }
    scene_.init();
    lighting_.resetDefaults();

    depthEstimator_.config.pythonExe = defaultPythonExecutable(paths_.root.u8string());
    depthEstimator_.config.scriptPath = paths_.aiScript.u8string();
    depthEstimator_.config.outputDir = paths_.cacheDir.u8string();
    depthEstimator_.config.dummy = options.dummyDepth;

    loadDemoScene();

    if (!options.imagePath.empty()) {
        if (!options.depthPath.empty()) autoEstimate_ = false;
        loadImage(options.imagePath);
    }
    if (!options.depthPath.empty()) loadDepthFile(options.depthPath);
    return true;
}

void App::shutdown() {
    photoTexture_.destroy();
    originalPreview_.destroy();
    depthPreview_.destroy();
    scene_.shutdown();
    renderer_.shutdown();
    if (window_) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    glfwTerminate();
}

void App::run() {
    double last = glfwGetTime();
    int smokeFrames = 0;

    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();
        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(window_, &fbW, &fbH);
        if (fbW == 0 || fbH == 0) {  // minimized
            glfwWaitEventsTimeout(0.1);
            continue;
        }

        const double now = glfwGetTime();
        const float dt = static_cast<float>(now - last);
        cpuFrameMs_ = (now - last) * 1000.0;
        last = now;

        pollDepthJob();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        handleInput(dt);
        if (showUI_) drawUI();
        if (rebuildRequested_) {
            rebuildRequested_ = false;
            rebuildMesh();
        }
        ImGui::Render();

        RenderTarget target;
        target.width = fbW;
        target.height = fbH;
        renderer_.render(scene_, camera_, lighting_, target);

        if (screenshotRequested_) {  // scene only, without UI
            screenshotRequested_ = false;
            const std::string path = (paths_.outputDir / ("screenshot_" + timestamp() + ".png")).u8string();
            saveScreenshot(fbW, fbH, path);
        }

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Smoke-test mode: wait for any depth job, render a few frames, save, quit.
        if (!options_.screenshotPath.empty() && !depthEstimator_.busy()) {
            if (smokeFrames == 0) {
                camera_.yaw = glm::radians(options_.yawDeg);
                camera_.pitch = glm::radians(options_.pitchDeg);
            }
            if (++smokeFrames == 8) {
                saveScreenshot(fbW, fbH, options_.screenshotPath);
                if (options_.exportOriginalView) exportOriginalViewRenders();
                glfwSetWindowShouldClose(window_, GLFW_TRUE);
            }
        }

        glfwSwapBuffers(window_);
        updateBenchmark(cpuFrameMs_);
    }
}

// ===========================================================================
// Pipeline
// ===========================================================================
void App::setStatus(StatusLevel level, const std::string& text) {
    statusLevel_ = level;
    statusText_ = text;
    std::printf("[status] %s\n", text.c_str());
}

void App::loadDemoScene() {
    ImageRGBA rgb;
    DepthImage depth;
    makeDemoInputs(rgb, depth);
    photoName_ = "demo";
    currentImagePath_.clear();
    originalPreview_.create(rgb, false);
    photo_ = std::move(rgb);
    photoTexture_.create(photo_);
    applyDepth(std::move(depth));
    setStatus(StatusLevel::Info, "Demo scene loaded. Drop a photo onto the window to begin.");
}

bool App::loadImage(const std::string& path) {
    if (depthEstimator_.busy()) {
        setStatus(StatusLevel::Error, "Please wait for the current depth estimation to finish.");
        return false;
    }
    std::string error;
    int w = 0, h = 0;
    if (!ImageLoader::validateImageFile(path, &error, &w, &h)) {
        setStatus(StatusLevel::Error, error);
        return false;
    }
    ImageRGBA img;
    if (!ImageLoader::loadRGBA(path, img, &error)) {
        setStatus(StatusLevel::Error, error);
        return false;
    }

    currentImagePath_ = path;
    imagePathInput_ = path;
    photoName_ = fs::u8path(path).stem().u8string();
    originalPreview_.create(img, false);
    photo_ = std::move(img);
    photoTexture_.create(photo_);
    depth_ = DepthImage{};
    depthPreview_.destroy();
    showFlatPlaceholder();

    setStatus(StatusLevel::Info, "Loaded " + photoName_ + " (" + std::to_string(w) + "x" +
                                     std::to_string(h) + ").");
    if (autoEstimate_) startDepthEstimation();
    return true;
}

void App::showFlatPlaceholder() {
    // Until depth arrives, show the photo on a flat plane (Experiment 5 baseline).
    DepthImage flat;
    flat.width = std::max(2, photo_.width / 8);
    flat.height = std::max(2, photo_.height / 8);
    flat.values.assign(static_cast<size_t>(flat.width) * flat.height, 0.5f);
    ReconstructionSettings s = recon_;
    s.flatPlane = true;
    s.gridResolution = 8;
    ReconstructionResult r = reconstructMesh(flat, s);
    if (r.ok) {
        scene_.setReconstruction(r.mesh, photoTexture_.id());
        reconStats_ = r.stats;
        frameOriginalView();
    }
}

void App::startDepthEstimation() {
    if (currentImagePath_.empty()) {
        setStatus(StatusLevel::Error, "Load a photo first (the demo scene has no file on disk).");
        return;
    }
    if (!depthEstimator_.start(currentImagePath_)) {
        setStatus(StatusLevel::Error, "A depth estimation job is already running.");
        return;
    }
    setStatus(StatusLevel::Busy, "Estimating depth for " + photoName_ + "...");
}

void App::pollDepthJob() {
    DepthJobResult r;
    if (!depthEstimator_.poll(r)) return;

    depthSeconds_ = r.seconds;
    lastLog_ = r.log;
    if (!r.ok) {
        setStatus(StatusLevel::Error, "Depth estimation failed: " + r.error);
        return;
    }

    ImageRGBA rgb;
    DepthImage depth;
    std::string error;
    if (!ImageLoader::loadRGBA(r.rgbPath, rgb, &error) ||
        !ImageLoader::loadDepth(r.depthPath, depth, &error)) {
        setStatus(StatusLevel::Error, error);
        return;
    }
    // Use Python's preprocessed RGB as the texture: it has the same orientation
    // (EXIF applied) and resolution as the depth map, so they line up exactly.
    photo_ = std::move(rgb);
    photoTexture_.create(photo_);
    depthPathInput_ = r.depthPath;
    applyDepth(std::move(depth));

    char buf[128];
    std::snprintf(buf, sizeof(buf), "Depth estimated in %.1f s.", r.seconds);
    setStatus(StatusLevel::Success, buf);
}

bool App::loadDepthFile(const std::string& path) {
    std::string error;
    if (!ImageLoader::validateImageFile(path, &error)) {
        setStatus(StatusLevel::Error, error);
        return false;
    }
    DepthImage depth;
    if (!ImageLoader::loadDepth(path, depth, &error)) {
        setStatus(StatusLevel::Error, error);
        return false;
    }
    std::string note;
    if (!photo_.empty()) {
        const float a1 = static_cast<float>(photo_.width) / photo_.height;
        const float a2 = static_cast<float>(depth.width) / depth.height;
        if (std::abs(a1 - a2) / a1 > 0.02f) note = " Warning: aspect ratio differs from the photo.";
    }
    depthPathInput_ = path;
    applyDepth(std::move(depth));
    setStatus(note.empty() ? StatusLevel::Success : StatusLevel::Error,
              "Loaded depth map " + fs::u8path(path).filename().u8string() + "." + note);
    return true;
}

void App::applyDepth(DepthImage depth) {
    depth_ = std::move(depth);
    depthPreview_.create(ImageLoader::colorizeDepth(depth_), false);
    rebuildMesh();
    frameOriginalView();
    lighting_.placePointLights(scene_.center(), scene_.radius());
}

void App::rebuildMesh() {
    if (depth_.empty()) return;
    ReconstructionResult r = reconstructMesh(depth_, recon_);
    if (!r.ok) {
        setStatus(StatusLevel::Error, "Reconstruction failed: " + r.error);
        return;
    }
    reconStats_ = r.stats;
    scene_.setReconstruction(r.mesh, photoTexture_.id());
}

void App::frameOriginalView() {
    // Eye at the origin (where the photo was taken), looking at the mesh center.
    const float zc = std::max(-scene_.center().z, 0.1f);
    camera_.setHome(glm::vec3(0.0f, 0.0f, -zc), zc, 0.0f, 0.0f, recon_.fovYDeg);
    camera_.reset();
}

void App::resetAll() {
    recon_ = ReconstructionSettings{};
    lighting_.resetDefaults();
    renderer_.settings = RenderSettings{};
    scene_.setEnvironment(0);
    scene_.showGround = true;
    scene_.showSky = true;
    if (!depth_.empty()) {
        rebuildMesh();
        lighting_.placePointLights(scene_.center(), scene_.radius());
    }
    frameOriginalView();
    setStatus(StatusLevel::Info, "All settings reset to defaults.");
}

void App::onFileDrop(int count, const char** paths) {
    if (count <= 0) return;
    const std::string path = paths[0];
    std::string lower = fs::u8path(path).filename().u8string();
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    // A file with "depth" in its name is treated as a depth map; anything else is a photo.
    if (lower.find("depth") != std::string::npos) loadDepthFile(path);
    else loadImage(path);
}

// ===========================================================================
// Input
// ===========================================================================
void App::handleInput(float dt) {
    ImGuiIO& io = ImGui::GetIO();
    int winW = 0, winH = 0;
    glfwGetWindowSize(window_, &winW, &winH);

    if (!io.WantCaptureMouse) {
        const bool left = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const bool panDrag = ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
                             ImGui::IsMouseDown(ImGuiMouseButton_Middle) || (left && io.KeyShift);
        if (panDrag) camera_.pan(io.MouseDelta.x, io.MouseDelta.y, static_cast<float>(winH));
        else if (left) camera_.orbit(io.MouseDelta.x, io.MouseDelta.y);
        if (io.MouseWheel != 0.0f) camera_.zoom(io.MouseWheel);
    }

    if (!io.WantCaptureKeyboard) {
        const float speed = 1.5f * dt;
        if (ImGui::IsKeyDown(ImGuiKey_LeftArrow) || ImGui::IsKeyDown(ImGuiKey_A)) camera_.orbitRadians(speed, 0);
        if (ImGui::IsKeyDown(ImGuiKey_RightArrow) || ImGui::IsKeyDown(ImGuiKey_D)) camera_.orbitRadians(-speed, 0);
        if (ImGui::IsKeyDown(ImGuiKey_UpArrow) || ImGui::IsKeyDown(ImGuiKey_W)) camera_.orbitRadians(0, speed);
        if (ImGui::IsKeyDown(ImGuiKey_DownArrow) || ImGui::IsKeyDown(ImGuiKey_S)) camera_.orbitRadians(0, -speed);
        if (ImGui::IsKeyDown(ImGuiKey_E) || ImGui::IsKeyDown(ImGuiKey_Equal)) camera_.zoom(4.0f * dt);
        if (ImGui::IsKeyDown(ImGuiKey_Q) || ImGui::IsKeyDown(ImGuiKey_Minus)) camera_.zoom(-4.0f * dt);
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) camera_.reset();
        if (ImGui::IsKeyPressed(ImGuiKey_H, false)) showUI_ = !showUI_;
        if (ImGui::IsKeyPressed(ImGuiKey_L, false))
            lighting_.settings.lightingEnabled = !lighting_.settings.lightingEnabled;
        if (ImGui::IsKeyPressed(ImGuiKey_P, false)) screenshotRequested_ = true;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }
}

// ===========================================================================
// Experiment tools
// ===========================================================================
std::string App::timestamp() const {
    const std::time_t t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", std::localtime(&t));
    return buf;
}

static ImageRGBA readPixelsFlipped(int w, int h) {
    ImageRGBA img;
    img.width = w;
    img.height = h;
    img.pixels.resize(static_cast<size_t>(w) * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, img.pixels.data());
    // OpenGL's origin is bottom-left; image files are top-left.
    const size_t row = static_cast<size_t>(w) * 4;
    std::vector<unsigned char> tmp(row);
    for (int y = 0; y < h / 2; ++y) {
        unsigned char* a = img.pixels.data() + y * row;
        unsigned char* b = img.pixels.data() + (h - 1 - y) * row;
        std::copy(a, a + row, tmp.data());
        std::copy(b, b + row, a);
        std::copy(tmp.data(), tmp.data() + row, b);
    }
    for (size_t i = 3; i < img.pixels.size(); i += 4) img.pixels[i] = 255;
    return img;
}

void App::saveScreenshot(int fbWidth, int fbHeight, const std::string& path) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    const ImageRGBA img = readPixelsFlipped(fbWidth, fbHeight);
    if (ImageLoader::savePNG(path, img)) setStatus(StatusLevel::Success, "Saved " + path);
    else setStatus(StatusLevel::Error, "Could not write " + path);
}

void App::exportOriginalViewRenders() {
    if (!scene_.hasReconstruction() || photo_.empty()) {
        setStatus(StatusLevel::Error, "Nothing to export yet.");
        return;
    }
    const int w = photo_.width, h = photo_.height;

    // Off-screen framebuffer at the photo's exact resolution.
    GLuint fbo = 0, color = 0, depthRb = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &color);
    glBindTexture(GL_TEXTURE_2D, color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    glGenRenderbuffers(1, &depthRb);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRb);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        // Same pose and frustum as the camera that took the photo.
        Camera cam;
        const float zc = std::max(-scene_.center().z, 0.1f);
        cam.target = glm::vec3(0.0f, 0.0f, -zc);
        cam.distance = zc;
        cam.fovYDeg = recon_.fovYDeg;

        const RenderSettings saved = renderer_.settings;
        RenderTarget target{fbo, w, h, false, false};
        const fs::path base = paths_.outputDir / photoName_;

        renderer_.settings.wireframe = false;
        renderer_.settings.viewMode = ViewMode::Unlit;
        renderer_.render(scene_, cam, lighting_, target);
        ImageLoader::savePNG(base.u8string() + "_render_unlit.png", readPixelsFlipped(w, h));

        renderer_.settings.viewMode = ViewMode::Lit;
        renderer_.render(scene_, cam, lighting_, target);
        ImageLoader::savePNG(base.u8string() + "_render_lit.png", readPixelsFlipped(w, h));

        ImageLoader::savePNG(base.u8string() + "_reference.png", photo_);
        renderer_.settings = saved;
        setStatus(StatusLevel::Success,
                  "Exported " + photoName_ + "_reference/_render_unlit/_render_lit.png to outputs/. "
                  "Compare with ai/evaluate.py.");
    } else {
        setStatus(StatusLevel::Error, "Off-screen framebuffer is incomplete.");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteRenderbuffers(1, &depthRb);
    glDeleteTextures(1, &color);
    glDeleteFramebuffers(1, &fbo);
}

void App::updateBenchmark(double frameMs) {
    if (!bench_.running) return;
    bench_.cpuMs.push_back(frameMs);
    bench_.gpuMs.push_back(renderer_.gpuFrameMs());
    if (static_cast<int>(bench_.cpuMs.size()) < bench_.targetFrames) return;

    bench_.running = false;
    std::vector<double> sorted = bench_.cpuMs;
    std::sort(sorted.begin(), sorted.end());
    const double avg = std::accumulate(sorted.begin(), sorted.end(), 0.0) / sorted.size();
    const double p95 = sorted[static_cast<size_t>(sorted.size() * 0.95) - 1];
    const double gpuAvg =
        std::accumulate(bench_.gpuMs.begin(), bench_.gpuMs.end(), 0.0) / bench_.gpuMs.size();

    const fs::path csv = paths_.outputDir / ("benchmark_" + timestamp() + ".csv");
    std::ofstream out(csv);
    out << "# image," << photoName_ << "\n# grid," << reconStats_.gridCols << "x"
        << reconStats_.gridRows << "\n# triangles," << reconStats_.triangleCount
        << "\n# shadows," << (lighting_.settings.shadowsEnabled ? 1 : 0)
        << "\n# vsync," << (vsync_ ? 1 : 0) << "\nframe,cpu_frame_ms,gpu_render_ms\n";
    for (size_t i = 0; i < bench_.cpuMs.size(); ++i)
        out << i << "," << bench_.cpuMs[i] << "," << bench_.gpuMs[i] << "\n";

    char buf[256];
    std::snprintf(buf, sizeof(buf), "avg %.2f ms (%.0f FPS), p95 %.2f ms, GPU %.2f ms",
                  avg, 1000.0 / avg, p95, gpuAvg);
    bench_.summary = buf;
    setStatus(StatusLevel::Success, std::string("Benchmark: ") + buf + " -> " + csv.filename().u8string());
}
