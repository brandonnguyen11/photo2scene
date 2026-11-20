# Interfaces and data contracts

Agree on this file before changing either side. It is the boundary between Member 3 (Python)
and Members 4/5 (C++).

## 1. C++ → Python: command

The app runs (on a worker thread, see `DepthEstimator.cpp`):

```
<python> ai/depth_estimation.py --input <photo> --output-dir <root>/cache
         --name <photo stem> --device auto|cpu|cuda|mps --model small|base|large [--dummy]
         > cache/<name>_log.txt 2>&1
```

| Exit code | Meaning | App behavior |
|---|---|---|
| 0 | success | loads outputs below |
| 2 | bad input file | shows the `ERROR:` line |
| 3 | model / package / write failure | shows the `ERROR:` line |
| 127 / 9009 | interpreter not found (POSIX / Windows) | asks the user to set the Python path |

On failure the script must print exactly one line starting with `ERROR:` to stderr. The app shows
the text after it to the user.

## 2. Python → C++: files

All files are written to `--output-dir` with the prefix `<name>`.

| File | Format | Contract |
|---|---|---|
| `<name>_rgb.png` | 8-bit BGR→RGB PNG | The preprocessed photo: EXIF orientation applied, longest side ≤ 2048. **The app uses this as the texture** so it is pixel-aligned with the depth. |
| `<name>_depth16.png` | 16-bit single-channel PNG, same width/height as `_rgb.png` | Normalized relative **inverse** depth. `65535` = nearest, `0` = farthest. Row 0 = top of the image. |
| `<name>_depth_vis.png` | 8-bit color PNG | Preview only (inferno colormap). Not read by the app. |
| `<name>_meta.json` | JSON | `model`, `device`, `width`, `height`, `model_load_ms`, `inference_ms`, `total_ms`, `raw_min`, `raw_max`, `convention`, `outputs`. Used for experiment timing tables. |

Normalization uses the 0.5th–99.5th percentiles of the raw prediction, so a few outlier pixels
don't compress the whole range.

The C++ side also accepts any 8-bit or 16-bit grayscale PNG with the same convention
(*Image ▸ Use an existing depth map*). That lets you test geometry without Python, or compare
depth from other models.

## 3. Depth → geometry (Member 4)

Given normalized value `d ∈ [0,1]` (1 = near), the user's `near`/`far` distances and vertical FOV:

```
Disparity mode (default):  1/Z = 1/far + d * (1/near - 1/far)
Linear mode:               Z   = far  + d * (near - far)

halfH = tan(fov/2),  halfW = halfH * (width / height)
vertex(u, v) = Z * ( (2u-1)*halfW,  (1-2v)*halfH,  -1 )      u,v ∈ [0,1], v = 0 at top
uv(u, v)     = (u, v)                                         texture row 0 = image top
```

The original camera sits at the origin looking down −Z. "Original viewpoint" restores exactly this
pose and FOV.

A triangle is dropped when `max(Z)/min(Z) − 1 > edge threshold` (a depth discontinuity), or when
any corner is below the background clip.

## 4. C++ module boundaries

| Producer | Type | Consumer |
|---|---|---|
| `ImageLoader` | `ImageRGBA`, `DepthImage` (`include/Image.h`) | App, Reconstruction |
| `reconstructMesh()` | `MeshData` (`include/MeshData.h`, CPU only) | `Mesh::upload` (GPU) |
| `LightingSystem` | uniforms (`Lighting::upload`) | `shaders/fragment.glsl` |
| `Renderer::render` | draws a `Scene` with a `Camera` and `LightingSystem` into a `RenderTarget` | App |

`MeshData`, `Image.h` and `Reconstruction` contain no OpenGL. They build into `p2s_core` and are
unit-tested in `tests/test_main.cpp`.

## 5. Shader uniforms (fragment.glsl)

| Uniform | Type | Notes |
|---|---|---|
| `uAmbient` | vec3 | linear color × intensity |
| `uSun.{enabled,direction,color}` | bool, vec3, vec3 | direction = where light travels; color is linear × intensity |
| `uPoints[i].{enabled,position,color,attenuation}` | up to `MAX_POINT_LIGHTS` (4) | attenuation = (constant, linear, quadratic) |
| `uBlinn`, `uShininess`, `uSpecularStrength` | bool, float, float | |
| `uShadowsEnabled`, `uShadowMap`, `uLightSpace`, `uShadowBias`, `uPcfRadius` | | sun shadows only |
| `uViewMode` | int | 0 lit, 1 unlit, 2 normals, 3 depth |

If you change `kMaxPointLights` in `Lighting.h`, change `MAX_POINT_LIGHTS` in the shader too.
