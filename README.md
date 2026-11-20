# Photo2Scene

Interactive single-image 3D reconstruction and relighting. Load a photo, estimate its depth with
Depth Anything V2, turn the depth into a textured mesh, and explore it under your own lights in a
custom OpenGL renderer.

```
photo ─▶ Python (Depth Anything V2) ─▶ 16-bit depth PNG ─▶ C++ mesh builder ─▶ OpenGL + GLSL lighting
```

## Features

| PRD section | What's implemented | Where |
|---|---|---|
| 3.1 Image upload | Drag & drop or path box, JPG/JPEG/PNG validation, size limits, previews, error messages | `ImageLoader.cpp`, `UI.cpp` |
| 3.2 Depth estimation | Depth Anything V2 (small/base/large) via PyTorch, run async from C++; EXIF-aware preprocessing; 16-bit export | `ai/depth_estimation.py`, `DepthEstimator.cpp` |
| 3.3 Reconstruction | Pinhole back-projection, adjustable resolution/FOV/depth range, bilateral smoothing, discontinuity cutting, background clip, smooth normals | `Reconstruction.cpp` |
| 3.4 Rendering engine | Shader/mesh/texture/camera classes, multi-pass renderer, GPU timer queries | `Renderer.cpp` and friends |
| 3.5 Lighting | Directional + 4 point lights, Lambert diffuse, Phong / Blinn-Phong specular, ambient, attenuation, PCF shadow mapping, sRGB-correct shading | `Lighting.cpp`, `shaders/fragment.glsl` |
| 3.6 Camera | Orbit / pan / zoom (mouse + keyboard), reset, "original viewpoint" | `Camera.cpp` |
| 3.7 UI | Dear ImGui panels for every setting, status + errors, previews, reset | `UI.cpp` |
| 3.8 Environment | Ground plane, gradient sky, 4 presets that also set matching light | `Scene.cpp` |
| 6 Experiments | Benchmark → CSV, screenshots, original-view export + PSNR/SSIM script, flat-plane baseline, normal/depth views | `App.cpp`, `ai/evaluate.py` |

---

## 1. Prerequisites

| | Windows | macOS | Linux |
|---|---|---|---|
| Compiler | Visual Studio 2022 (Desktop C++ workload) | Xcode Command Line Tools (`xcode-select --install`) | GCC 9+ or Clang 10+ |
| CMake | 3.20+ (bundled with VS, or cmake.org) | `brew install cmake` | `apt install cmake` |
| Git | required (CMake downloads GLFW, GLM, ImGui) | | |
| Python | 3.9–3.12 for the AI step | | |
| Linux only | | | `apt install libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev` |

Your GPU needs **OpenGL 4.1 core**. That's every Mac since ~2012 and any Windows GPU driver from the last decade.

## 2. Build the C++ app

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

The first configure downloads GLFW 3.4, GLM 1.0.1 and Dear ImGui 1.91.9. GLAD (OpenGL 4.1 core)
and stb are already checked in under `external/`.

Run the unit tests:

```bash
ctest --test-dir build -C Release --output-on-failure
```

Run the app (it starts with a procedural demo scene, so you can test rendering with no Python):

```bash
./build/Photo2Scene                       # macOS / Linux
build\Release\Photo2Scene.exe             # Windows (Visual Studio generator)
```

**VS Code:** install the *CMake Tools* and *C/C++* extensions, open the folder, pick a kit, press
*Build*. `compile_commands.json` is generated for IntelliSense.

## 3. Set up the Python depth model

Create the virtual environment inside `ai/.venv`. The app finds it automatically there.

```bash
cd ai
python -m venv .venv
# Windows:  .venv\Scripts\activate      macOS/Linux:  source .venv/bin/activate
pip install --upgrade pip
pip install -r requirements.txt
```

- **NVIDIA GPU (Windows/Linux):** install the CUDA build of PyTorch first from
  https://pytorch.org/get-started/locally/, then `pip install -r requirements.txt`.
- **Apple Silicon:** the default PyTorch wheel uses the GPU (`mps`) automatically.
- The first run downloads the model (~100 MB) from Hugging Face and caches it.

Test it on its own:

```bash
python depth_estimation.py --input ../assets/images/your_photo.jpg --output-dir ../cache
python depth_estimation.py --input any.jpg --output-dir ../cache --dummy   # no model, tests plumbing
```

To use a different interpreter, set the `P2S_PYTHON` environment variable or change it under
*Image ▸ Python settings* in the app.

## 4. Using the app

1. Drag a JPG/PNG onto the window, or paste a path and click **Load**.
2. The photo appears as a flat plane while the depth runs (status shows elapsed time).
3. The mesh appears, viewed from where the photo was taken. Orbit to see the depth.
4. Adjust lights in **4. Lighting**. Nothing is recomputed except the shading.

| Input | Action |
|---|---|
| Left drag / arrows / WASD | Orbit |
| Right drag, middle drag, Shift + left drag | Pan |
| Wheel, Q / E | Zoom |
| R | Reset camera |
| L | Toggle lighting |
| H | Hide/show UI (clean demo view) |
| P | Screenshot (scene only) → `outputs/` |
| Esc | Quit |

Files with "depth" in the name that you drop are loaded as depth maps instead of photos.

**Getting good results**
- Raise **Far distance** to exaggerate depth; lower it for a subtler effect.
- If edges look smeared, lower **Edge threshold**. If the subject has holes, raise it.
- For portraits, raise **Background clip** to isolate the person.
- Set **Photo FOV** to roughly match the camera (phones are about 45–55°).

### Command line (for scripted experiments)

```
Photo2Scene [--image photo.jpg] [--depth depth.png] [--dummy] [--size WxH]
            [--screenshot out.png] [--yaw deg] [--pitch deg] [--export]
```

`--screenshot` renders, saves and exits after any depth job finishes. With `--export` it also writes
the original-view renders (see below). Example batch run:

```bash
for f in assets/images/*.jpg; do
  ./build/Photo2Scene --image "$f" --screenshot "outputs/$(basename "$f" .jpg)_yaw35.png" --yaw 35 --export
done
```

## 5. Running the experiments (PRD §6)

| Experiment | How |
|---|---|
| 1. Depth estimation | Run the categories through the app; `cache/*_depth_vis.png` and `*_meta.json` give maps and timings. Switch **Model** for comparisons. |
| 2. Geometry | Vary **Mesh resolution** / smoothing / edge threshold. Stats line shows vertices, triangles, dropped and build ms. Use **Normals** and **Wireframe** views. |
| 3. Lighting | Sweep sun azimuth/elevation, colors, shininess, Phong vs Blinn-Phong, shadows on/off. Screenshot each. |
| 4. Performance | Turn **VSync off**, click **Run benchmark** → `outputs/benchmark_*.csv` (CPU frame ms + GPU render ms per frame). |
| 5. Visual comparison | Original photo vs **Flat image plane** vs depth mesh vs depth + lighting. For numbers, click **Export original-view renders**, then: `python ai/evaluate.py outputs/NAME_reference.png outputs/NAME_render_*.png --csv results.csv` |

Our sanity check: the *unlit* original-view render of a test image scores about **35 dB PSNR / 0.97
SSIM** against the photo, which shows the projection and texture mapping are correct. The lit render
scores lower *by design*, because lighting changes the appearance. PSNR/SSIM compare 2D images, not
3D accuracy (PRD §6.3).

## 6. Project structure and ownership

```
Photo2Scene/
├── src/ include/
│   ├── main.cpp                 entry point, CLI                         (M5)
│   ├── App.*                    window, main loop, pipeline glue, tools  (M5)
│   ├── UI.cpp                   all ImGui panels                         (M5)
│   ├── Renderer.*               passes, GPU timer                        (M1)
│   ├── Shader.* Mesh.* Texture.* Camera.* Scene.*                         (M1)
│   ├── Lighting.*               lights, material, shadow map             (M2)
│   ├── DepthEstimator.*         async Python bridge                      (M3/M5)
│   ├── Reconstruction.* MeshData.*  depth → mesh                          (M4)
│   └── ImageLoader.* Image.h    file I/O, validation, depth preview      (M3/M5)
├── shaders/
│   ├── vertex.glsl fragment.glsl          lit scene                      (M2)
│   ├── shadow_vertex.glsl shadow_fragment.glsl                           (M2)
│   ├── sky_*.glsl unlit_fragment.glsl                                    (M1)
├── ai/
│   ├── depth_estimation.py      model, preprocessing, export             (M3)
│   ├── evaluate.py              PSNR / SSIM                              (M3/M5)
│   └── requirements.txt
├── tests/test_main.cpp          reconstruction + I/O unit tests          (M4/M5)
├── docs/INTERFACES.md           Python ↔ C++ data contract (read this first)
├── external/                    GLAD (GL 4.1 core), stb
├── assets/{images,textures,environments}/
├── cache/                       depth outputs (git-ignored)
└── outputs/                     screenshots, benchmarks, exports (git-ignored)
```

Edit shaders while the app runs, then click **Performance ▸ Reload shaders**. A compile error is shown
in the status line and the old shader stays active.

## 7. Known limitations (2.5D by design)

- **Hidden surfaces don't exist.** Orbiting far reveals holes where foreground occluded background.
- **Relative depth only.** The model's scale and offset are unknown, so near/far are user-chosen.
- **The photo already contains its original lighting.** New light is added on top of it. Strong
  original shadows will still be visible. Removing them (intrinsic decomposition) is a possible stretch goal.
- **Only the sun casts shadows.** Point-light shadows would need cube maps (stretch goal).
- The UV mapping is exact at vertices. Within a triangle it is perspective-interpolated, which
  explains the remaining error in the PSNR sanity check.

## 8. Troubleshooting

| Symptom | Fix |
|---|---|
| "Failed to create an OpenGL 4.1 core window" | Update GPU drivers. On a Windows VM / RDP, use a machine with a real GPU. |
| "Python interpreter not found" | Create `ai/.venv` (section 3) or set the path in *Python settings*. |
| "Missing Python package (torch)" | Activate the venv and `pip install -r ai/requirements.txt`. |
| First depth run is slow | It is downloading the model. Later runs take ~1–5 s on CPU and are faster on GPU. |
| CMake can't download dependencies | Check internet/proxy, or delete `build/` and re-configure. |
| Shader errors after editing | Read the status line, fix, click *Reload shaders*. |
