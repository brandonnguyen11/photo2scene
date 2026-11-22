// UI.cpp — Dear ImGui interface (Member 5: User Interface)
//
// Every panel edits plain settings structs; the App reacts (rebuilds the mesh
// only when geometry settings are committed, never for lighting changes).

#include "App.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace {

const ImVec4 kColorInfo(0.75f, 0.78f, 0.82f, 1.0f);
const ImVec4 kColorBusy(1.0f, 0.82f, 0.35f, 1.0f);
const ImVec4 kColorOk(0.45f, 0.9f, 0.5f, 1.0f);
const ImVec4 kColorError(1.0f, 0.45f, 0.4f, 1.0f);

void helpMarker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

ImTextureID texId(GLuint id) { return static_cast<ImTextureID>(static_cast<intptr_t>(id)); }

std::string formatBytes(size_t bytes) {
    char buf[64];
    if (bytes >= (1u << 20)) std::snprintf(buf, sizeof(buf), "%.1f MB", bytes / 1048576.0);
    else std::snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
    return buf;
}

}  // namespace

void App::drawUI() {
    drawControlPanel();
    drawPreviewPanel();
}

void App::drawControlPanel() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 10, vp->WorkPos.y + 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, vp->WorkSize.y - 20), ImGuiCond_FirstUseEver);
    ImGui::Begin("Photo2Scene");

    // Status line
    const ImVec4 color = statusLevel_ == StatusLevel::Error   ? kColorError
                         : statusLevel_ == StatusLevel::Busy  ? kColorBusy
                         : statusLevel_ == StatusLevel::Success ? kColorOk
                                                                : kColorInfo;
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    if (depthEstimator_.busy()) {
        static const char spinner[] = "|/-\\";
        ImGui::TextWrapped("%c Estimating depth... %.1f s",
                           spinner[static_cast<int>(ImGui::GetTime() * 8) % 4],
                           depthEstimator_.elapsedSeconds());
    } else {
        ImGui::TextWrapped("%s", statusText_.c_str());
    }
    ImGui::PopStyleColor();
    ImGui::Separator();

    if (ImGui::CollapsingHeader("0. Immersive World", ImGuiTreeNodeFlags_DefaultOpen))
        drawWorldSection();
    if (ImGui::CollapsingHeader("1. Image")) drawImageSection();
    if (ImGui::CollapsingHeader("2. Reconstruction")) drawReconstructionSection();
    if (ImGui::CollapsingHeader("3. Camera")) drawCameraSection();
    if (ImGui::CollapsingHeader("4. Lighting")) drawLightingSection();
    if (ImGui::CollapsingHeader("5. Environment & View")) drawEnvironmentSection();
    if (ImGui::CollapsingHeader("6. Performance & Experiments")) drawPerformanceSection();

    ImGui::Separator();
    if (ImGui::Button("Reset everything to defaults")) resetAll();
    ImGui::TextDisabled("H hide UI | R reset camera | L lighting | P screenshot");
    ImGui::End();
}

void App::drawImageSection() {
    ImGui::TextWrapped("Drag & drop a JPG/PNG onto the window, or paste a path:");
    ImGui::TextDisabled("HEIC/WebP not supported — export as JPG or PNG first.");
    ImGui::SetNextItemWidth(-60);
    const bool enter = ImGui::InputText("##imagepath", &imagePathInput_,
                                        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Load") || enter) loadImage(imagePathInput_);

    ImGui::Checkbox("Estimate depth automatically", &autoEstimate_);
    if (immersiveMode_) {
        ImGui::SliderFloat("Photo height (m)", &photoHeightM_, 1.2f, 2.2f, "%.2f");
        if (ImGui::IsItemDeactivatedAfterEdit() && scene_.hasReconstruction()) placePhotoInWorld();
    }
    ImGui::BeginDisabled(depthEstimator_.busy() || currentImagePath_.empty());
    if (ImGui::Button("Estimate depth now")) startDepthEstimation();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Load demo scene")) loadDemoScene();

    if (ImGui::TreeNode("Use an existing depth map")) {
        ImGui::TextWrapped("16-bit or 8-bit grayscale PNG, bright = near.");
        ImGui::SetNextItemWidth(-60);
        ImGui::InputText("##depthpath", &depthPathInput_);
        ImGui::SameLine();
        if (ImGui::Button("Load##depth")) loadDepthFile(depthPathInput_);
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Python settings")) {
        DepthEstimatorConfig& cfg = depthEstimator_.config;
        ImGui::InputText("Python", &cfg.pythonExe);
        helpMarker("Interpreter with the packages from ai/requirements.txt. "
                   "A venv at ai/.venv is picked up automatically.");

        static const char* devices[] = {"auto", "cpu", "cuda", "mps"};
        int dev = 0;
        for (int i = 0; i < 4; ++i)
            if (cfg.device == devices[i]) dev = i;
        if (ImGui::Combo("Device", &dev, devices, 4)) cfg.device = devices[dev];

        static const char* models[] = {"small", "base", "large"};
        int mdl = 0;
        for (int i = 0; i < 3; ++i)
            if (cfg.model == models[i]) mdl = i;
        if (ImGui::Combo("Model", &mdl, models, 3)) cfg.model = models[mdl];
        helpMarker("Depth Anything V2. 'small' is Apache-2.0; 'base' and 'large' are "
                   "CC-BY-NC-4.0 (non-commercial) and much slower.");

        ImGui::Checkbox("Dummy depth (no AI, for testing)", &cfg.dummy);
        ImGui::TextDisabled("Script: %s", cfg.scriptPath.c_str());
        if (!lastLog_.empty() && ImGui::TreeNode("Last Python log")) {
            ImGui::BeginChild("log", ImVec2(0, 140), ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::TextUnformatted(lastLog_.c_str());
            ImGui::EndChild();
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }
}

void App::drawReconstructionSection() {
    bool commit = false;
    // Sliders only rebuild when released, so dragging stays smooth (req. 4.1).
    auto onRelease = [&] {
        if (ImGui::IsItemDeactivatedAfterEdit()) commit = true;
    };

    ImGui::SliderInt("Mesh resolution", &recon_.gridResolution, 32, 1024);
    onRelease();
    helpMarker("Vertices along the image's longest side.");
    ImGui::SliderFloat("Photo FOV", &recon_.fovYDeg, 20.0f, 100.0f, "%.0f deg");
    onRelease();
    helpMarker("Assumed vertical field of view of the original camera. Phones are ~45-55 deg.");
    ImGui::SliderFloat("Near distance", &recon_.nearDistance, 0.2f, 5.0f, "%.2f");
    onRelease();
    ImGui::SliderFloat("Far distance", &recon_.farDistance, recon_.nearDistance + 0.05f, 20.0f, "%.2f");
    onRelease();
    helpMarker("The model gives relative depth only, so you choose the range. "
               "A larger far/near ratio exaggerates depth.");

    int mapping = static_cast<int>(recon_.mapping);
    if (ImGui::Combo("Depth values are", &mapping, "Inverse depth (disparity)\0Linear depth\0")) {
        recon_.mapping = static_cast<DepthMapping>(mapping);
        commit = true;
    }
    if (ImGui::Checkbox("Invert depth", &recon_.invertDepth)) commit = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Flat image plane", &recon_.flatPlane)) commit = true;

    ImGui::SeparatorText("Cleanup");
    ImGui::SliderInt("Smoothing passes", &recon_.smoothIterations, 0, 5);
    onRelease();
    ImGui::SliderFloat("Smoothing edge sigma", &recon_.smoothRangeSigma, 0.005f, 0.2f, "%.3f");
    onRelease();
    if (ImGui::Checkbox("Cut depth discontinuities", &recon_.cutDiscontinuities)) commit = true;
    ImGui::SliderFloat("Edge threshold", &recon_.discontinuityThreshold, 0.005f, 0.5f, "%.3f",
                       ImGuiSliderFlags_Logarithmic);
    onRelease();
    helpMarker("Triangles whose depth varies more than this (relative) are removed, "
               "which prevents stretched 'rubber sheets' between foreground and background.");
    ImGui::SliderFloat("Background clip", &recon_.backgroundClip, 0.0f, 0.9f, "%.2f");
    onRelease();
    helpMarker("Removes everything farther than this normalized depth (0 = keep all). "
               "Useful to isolate a portrait subject.");

    if (ImGui::Button("Rebuild mesh")) commit = true;
    if (commit) rebuildRequested_ = true;

    const ReconstructionStats& s = reconStats_;
    ImGui::Text("Grid %d x %d | %zu vertices | %zu triangles", s.gridCols, s.gridRows,
                s.vertexCount, s.triangleCount);
    ImGui::Text("Dropped %zu triangles | built in %.1f ms", s.droppedTriangles, s.timeMs);
}

void App::drawCameraSection() {
    ImGui::SliderFloat("Field of view", &camera_.fovYDeg, 15.0f, 110.0f, "%.0f deg");
    if (ImGui::Button("Reset camera")) camera_.reset();
    ImGui::SameLine();
    if (ImGui::Button("Original viewpoint")) frameOriginalView();
    helpMarker("Places the camera where the photo was taken. With lighting off, the view "
               "should match the original photo.");
    ImGui::TextDisabled("Left drag: orbit   Right/middle/Shift drag: pan");
    ImGui::TextDisabled("Wheel or Q/E: zoom   Arrows/WASD: orbit");
}

void App::drawLightingSection() {
    LightingSettings& L = lighting_.settings;
    ImGui::Checkbox("Enable lighting", &L.lightingEnabled);
    ImGui::BeginDisabled(!L.lightingEnabled);

    ImGui::SeparatorText("Ambient");
    ImGui::ColorEdit3("Ambient color", &L.ambientColor.x);
    ImGui::SliderFloat("Ambient intensity", &L.ambientIntensity, 0.0f, 1.5f);

    ImGui::SeparatorText("Directional light (sun)");
    ImGui::Checkbox("Sun enabled", &L.sun.enabled);
    ImGui::SliderFloat("Azimuth", &L.sun.azimuthDeg, -180.0f, 180.0f, "%.0f deg");
    ImGui::SliderFloat("Elevation", &L.sun.elevationDeg, 2.0f, 90.0f, "%.0f deg");
    ImGui::ColorEdit3("Sun color", &L.sun.color.x);
    ImGui::SliderFloat("Sun intensity", &L.sun.intensity, 0.0f, 4.0f);

    ImGui::SeparatorText("Point lights");
    for (int i = 0; i < kMaxPointLights; ++i) {
        PointLight& p = L.points[i];
        ImGui::PushID(i);
        ImGui::ColorButton("##swatch", ImVec4(p.color.r, p.color.g, p.color.b, 1.0f),
                           ImGuiColorEditFlags_NoTooltip, ImVec2(14, 14));
        ImGui::SameLine();
        ImGui::Checkbox("##on", &p.enabled);
        ImGui::SameLine();
        if (ImGui::TreeNode("light", "Point light %d", i + 1)) {
            ImGui::DragFloat3("Position", &p.position.x, 0.01f);
            if (ImGui::Button("Move to camera")) p.position = camera_.position();
            ImGui::ColorEdit3("Color", &p.color.x);
            ImGui::SliderFloat("Intensity", &p.intensity, 0.0f, 8.0f);
            ImGui::SliderFloat("Linear atten.", &p.linear, 0.0f, 2.0f);
            ImGui::SliderFloat("Quadratic atten.", &p.quadratic, 0.0f, 2.0f);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    ImGui::SeparatorText("Material");
    int model = static_cast<int>(L.specularModel);
    if (ImGui::Combo("Specular model", &model, "Phong\0Blinn-Phong\0"))
        L.specularModel = static_cast<SpecularModel>(model);
    ImGui::SliderFloat("Shininess", &L.shininess, 1.0f, 256.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Specular strength", &L.specularStrength, 0.0f, 1.0f);

    ImGui::SeparatorText("Shadows (sun)");
    ImGui::Checkbox("Shadows", &L.shadowsEnabled);
    ImGui::SliderFloat("Shadow bias", &L.shadowBias, 0.0001f, 0.02f, "%.4f",
                       ImGuiSliderFlags_Logarithmic);
    ImGui::SliderInt("Softness (PCF)", &L.pcfRadius, 0, 3);
    static const int sizes[] = {512, 1024, 2048, 4096};
    int sizeIdx = 2;
    for (int i = 0; i < 4; ++i)
        if (L.shadowMapSize == sizes[i]) sizeIdx = i;
    if (ImGui::Combo("Shadow map", &sizeIdx, "512\0" "1024\0" "2048\0" "4096\0"))
        L.shadowMapSize = sizes[sizeIdx];

    ImGui::EndDisabled();
    if (ImGui::Button("Reset lighting")) {
        lighting_.resetDefaults();
        lighting_.placePointLights(scene_.center(), scene_.radius());
    }
}

void App::drawWorldSection() {
    ImGui::Checkbox("Immersive world mode", &immersiveMode_);
    helpMarker("v2 worlds with analytic sky, terrain, grass and water. Turn off for the v1 "
               "photo-depth demo scene.");

    static const char* kWorlds[] = {"royal_garden", "beach", "forest"};
    static const char* kNames[] = {"Royal Garden", "Beach", "Forest"};
    int idx = 0;
    for (int i = 0; i < 3; ++i)
        if (currentWorldId_ == kWorlds[i]) idx = i;
    if (ImGui::Combo("World", &idx, kNames, 3)) loadWorld(kWorlds[idx]);

    SkySettings& sky = world_.sky().settings;
    ImGui::SeparatorText("Sky & sun");
    ImGui::SliderFloat("Time of day", &sky.timeOfDayHours, 0.0f, 24.0f, "%.2f h");
    ImGui::SliderFloat("Latitude", &sky.latitudeDeg, -60.0f, 60.0f, "%.1f deg");
    ImGui::SliderFloat("Turbidity", &sky.turbidity, 1.8f, 8.0f, "%.2f");
    ImGui::SliderFloat("Cloud cover", &sky.cloudCover, 0.0f, 1.0f);
    ImGui::SliderFloat("Fog density", &sky.fogDensity, 0.0f, 0.06f, "%.4f");
    ImGui::SliderFloat("Wind", &sky.windStrength, 0.0f, 1.5f);
    ImGui::Checkbox("Auto-advance time", &sky.autoAdvance);
    if (sky.autoAdvance)
        ImGui::SliderFloat("Hours / sec", &sky.autoAdvanceSpeed, 0.02f, 1.0f);

    const SkyState st = world_.sky().evaluate();
    ImGui::TextDisabled("Sun elev %.1f deg  |  %s", Sky::sunElevationDeg(sky),
                        st.isNight ? "night" : "day");

    ImGui::SeparatorText("Placement");
    ImGui::Checkbox("Show photo mesh in world", &showPhotoInWorld_);
    if (ImGui::Button("Re-place photo in world") && scene_.hasReconstruction()) placePhotoInWorld();
    if (ImGui::Button("Frame world view")) frameWorldView();
    ImGui::SameLine();
    if (ImGui::Button("Reload world JSON")) loadWorld(currentWorldId_);
    helpMarker("Edit assets/worlds/*.json (e.g. lighting.timeOfDay) and reload — W-4.");

    const auto& spots = world_.photoSpots();
    if (!spots.empty() && ImGui::BeginCombo("Photo spot", spots.front().name.c_str())) {
        for (const PhotoSpot& s : spots) {
            if (ImGui::Selectable(s.name.c_str())) {
                glm::vec3 t = s.position;
                t.y = world_.terrain().heightAt(t.x, t.z) + 1.4f;
                camera_.setHome(t, 18.0f, glm::radians(s.facingYawDeg), glm::radians(12.0f), 50.0f);
                camera_.reset();
            }
        }
        ImGui::EndCombo();
    }
}

void App::drawEnvironmentSection() {
    const auto& presets = environmentPresets();
    int env = scene_.environmentIndex();
    if (ImGui::BeginCombo("Environment", presets[env].name)) {
        for (int i = 0; i < static_cast<int>(presets.size()); ++i) {
            if (ImGui::Selectable(presets[i].name, i == env)) {
                scene_.setEnvironment(i);
                // Each environment suggests matching ambient and sun light.
                LightingSettings& L = lighting_.settings;
                L.ambientColor = presets[i].ambient;
                L.sun.color = presets[i].sunColor;
                L.sun.elevationDeg = presets[i].sunElevationDeg;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::Checkbox("Ground plane", &scene_.showGround);
    ImGui::SameLine();
    ImGui::Checkbox("Sky", &scene_.showSky);

    ImGui::SeparatorText("View");
    int mode = static_cast<int>(renderer_.settings.viewMode);
    if (ImGui::Combo("View mode", &mode, "Lit\0Unlit (photo only)\0Normals\0Depth\0"))
        renderer_.settings.viewMode = static_cast<ViewMode>(mode);
    ImGui::Checkbox("Wireframe", &renderer_.settings.wireframe);
    ImGui::SameLine();
    ImGui::Checkbox("Light gizmos", &renderer_.settings.showLightGizmos);
}

void App::drawPerformanceSection() {
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::Text("FPS: %.1f   (frame %.2f ms)", io.Framerate, 1000.0f / io.Framerate);
    ImGui::Text("GPU render time: %.2f ms", renderer_.gpuFrameMs());
    ImGui::Text("Depth estimation: %.2f s   Mesh build: %.1f ms", depthSeconds_, reconStats_.timeMs);
    const size_t gpuBytes = scene_.gpuBytes() + lighting_.gpuBytes() + photoTexture_.gpuBytes() +
                            originalPreview_.gpuBytes() + depthPreview_.gpuBytes();
    ImGui::Text("GPU memory (app resources): %s", formatBytes(gpuBytes).c_str());
    helpMarker("Estimated from buffers and textures this app allocated; driver overhead not included.");

    if (ImGui::Checkbox("VSync", &vsync_)) glfwSwapInterval(vsync_ ? 1 : 0);
    helpMarker("Turn off before benchmarking, or FPS is capped at the display refresh rate.");

    ImGui::SeparatorText("Experiments");
    if (ImGui::Button("Screenshot (P)")) screenshotRequested_ = true;
    ImGui::SameLine();
    if (ImGui::Button("Export original-view renders")) exportOriginalViewRenders();
    helpMarker("Renders from the photo's exact camera at the photo's resolution (unlit and lit) "
               "and saves the reference, for PSNR/SSIM with ai/evaluate.py (Experiment 5).");

    ImGui::BeginDisabled(bench_.running);
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("frames", &bench_.targetFrames, 50);
    if (bench_.targetFrames < 30) bench_.targetFrames = 30;
    ImGui::SameLine();
    if (ImGui::Button("Run benchmark")) {
        bench_.running = true;
        bench_.cpuMs.clear();
        bench_.gpuMs.clear();
    }
    ImGui::EndDisabled();
    if (bench_.running)
        ImGui::ProgressBar(static_cast<float>(bench_.cpuMs.size()) / bench_.targetFrames);
    if (!bench_.summary.empty()) ImGui::TextWrapped("Last: %s", bench_.summary.c_str());

    ImGui::SeparatorText("Developer");
    if (ImGui::Button("Reload shaders")) {
        std::string error;
        if (renderer_.reloadShaders(&error)) setStatus(StatusLevel::Success, "Shaders reloaded.");
        else setStatus(StatusLevel::Error, error);
    }
}

void App::drawPreviewPanel() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float width = 330.0f;
    // Anchored to the top-right corner so it stays visible when the window is resized.
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 10, vp->WorkPos.y + 10),
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(width, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Previews", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    const float w = width - 20.0f;
    auto preview = [&](const char* label, const Texture2D& tex) {
        ImGui::Text("%s (%dx%d)", label, tex.width(), tex.height());
        const float h = w * static_cast<float>(tex.height()) / std::max(tex.width(), 1);
        ImGui::Image(texId(tex.id()), ImVec2(w, h));
    };

    if (originalPreview_.valid()) preview("Original", originalPreview_);
    else ImGui::TextDisabled("No image loaded");
    ImGui::Spacing();
    if (depthPreview_.valid()) preview("Depth map, bright = near", depthPreview_);
    else ImGui::TextDisabled(depthEstimator_.busy() ? "Depth map: estimating..." : "No depth map yet");
    ImGui::End();
}
