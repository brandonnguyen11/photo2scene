#!/usr/bin/env python3
"""Photo2Scene depth estimation (Member 3: AI and Image Processing).

Turns one photograph into a depth map that the C++ app can read.

Usage:
    python depth_estimation.py --input photo.jpg --output-dir ../cache
    python depth_estimation.py --input photo.jpg --output-dir ../cache --dummy   # no model

Outputs (in --output-dir, <name> defaults to the input file's stem):
    <name>_rgb.png        preprocessed photo (EXIF-rotated, resized). Use as the texture.
    <name>_depth16.png    16-bit grayscale depth, 65535 = nearest, 0 = farthest.
    <name>_depth_vis.png  colorized preview for reports
    <name>_meta.json      timings, model, device, raw value range

Exit codes: 0 ok, 2 bad input, 3 model/runtime error. On failure a line starting
with "ERROR:" is printed to stderr (the C++ app shows it to the user).
See docs/INTERFACES.md for the full contract.
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

import cv2
import numpy as np

EXIT_OK = 0
EXIT_BAD_INPUT = 2
EXIT_MODEL_ERROR = 3

SUPPORTED_EXTENSIONS = {".jpg", ".jpeg", ".png"}
MIN_SIDE = 16

MODEL_IDS = {
    "small": "depth-anything/Depth-Anything-V2-Small-hf",  # Apache-2.0
    "base": "depth-anything/Depth-Anything-V2-Base-hf",    # CC-BY-NC-4.0
    "large": "depth-anything/Depth-Anything-V2-Large-hf",  # CC-BY-NC-4.0
}


class InputError(Exception):
    """Problem with the user's file (maps to exit code 2)."""


# ---------------------------------------------------------------------------
# Image loading and preprocessing
# ---------------------------------------------------------------------------
def read_image(path: Path) -> np.ndarray:
    """Read an image as BGR uint8. Handles non-ASCII paths and EXIF rotation."""
    if not path.is_file():
        raise InputError(f"File not found: {path}")
    if path.suffix.lower() not in SUPPORTED_EXTENSIONS:
        raise InputError(f"Unsupported format '{path.suffix}'. Use JPG, JPEG or PNG.")
    data = np.fromfile(str(path), dtype=np.uint8)
    # IMREAD_COLOR applies the EXIF orientation, so phone portraits come out upright.
    image = cv2.imdecode(data, cv2.IMREAD_COLOR)
    if image is None:
        raise InputError("The file could not be decoded as an image (corrupt or not an image).")
    h, w = image.shape[:2]
    if min(h, w) < MIN_SIDE:
        raise InputError(f"Image is too small ({w}x{h}). Minimum is {MIN_SIDE}x{MIN_SIDE}.")
    return image


def preprocess(image_bgr: np.ndarray, max_side: int) -> np.ndarray:
    """Downscale very large photos so texture upload and inference stay fast."""
    h, w = image_bgr.shape[:2]
    scale = max_side / max(h, w)
    if scale < 1.0:
        new_size = (max(1, round(w * scale)), max(1, round(h * scale)))
        image_bgr = cv2.resize(image_bgr, new_size, interpolation=cv2.INTER_AREA)
    return image_bgr


# ---------------------------------------------------------------------------
# Depth estimation
# ---------------------------------------------------------------------------
def pick_device(requested: str) -> str:
    import torch

    if requested != "auto":
        return requested
    if torch.cuda.is_available():
        return "cuda"
    if getattr(torch.backends, "mps", None) and torch.backends.mps.is_available():
        return "mps"
    return "cpu"


def estimate_depth_model(image_rgb: np.ndarray, model_key: str, device: str) -> tuple[np.ndarray, dict]:
    """Run Depth Anything V2. Returns relative inverse depth (larger = closer)."""
    import torch
    from transformers import AutoImageProcessor, AutoModelForDepthEstimation

    model_id = MODEL_IDS[model_key]
    device = pick_device(device)

    t0 = time.perf_counter()
    processor = AutoImageProcessor.from_pretrained(model_id)
    model = AutoModelForDepthEstimation.from_pretrained(model_id).to(device).eval()
    t_load = time.perf_counter() - t0

    inputs = processor(images=image_rgb, return_tensors="pt")
    inputs = {k: v.to(device) for k, v in inputs.items()}

    t1 = time.perf_counter()
    with torch.no_grad():
        predicted = model(**inputs).predicted_depth  # (1, H', W')
    # Resize back to the photo's resolution so pixels line up with the texture.
    # Done on the CPU: bicubic upsampling is not supported on every backend (e.g. MPS).
    h, w = image_rgb.shape[:2]
    predicted = predicted.float().cpu()
    t_infer = time.perf_counter() - t1
    depth = torch.nn.functional.interpolate(
        predicted.unsqueeze(1), size=(h, w), mode="bicubic", align_corners=False
    ).squeeze().numpy()

    info = {"model": model_id, "device": device,
            "model_load_ms": round(t_load * 1000, 1), "inference_ms": round(t_infer * 1000, 1)}
    return depth, info


def estimate_depth_dummy(image_rgb: np.ndarray) -> tuple[np.ndarray, dict]:
    """Synthetic depth for pipeline testing without PyTorch: a centered dome
    plus a floor that comes closer toward the bottom of the frame."""
    h, w = image_rgb.shape[:2]
    y, x = np.mgrid[0:h, 0:w].astype(np.float32)
    nx, ny = (x / w - 0.5) * 2.0, (y / h - 0.5) * 2.0
    dome = np.clip(1.0 - (nx ** 2 + ny ** 2), 0.0, 1.0) ** 0.5
    floor = np.clip((y / h - 0.6) / 0.4, 0.0, 1.0)
    depth = 0.2 + 0.6 * dome + 0.3 * floor
    return depth, {"model": "dummy", "device": "cpu", "model_load_ms": 0.0, "inference_ms": 0.0}


def normalize(depth: np.ndarray) -> tuple[np.ndarray, float, float]:
    """Robustly map to [0, 1] (1 = nearest). Percentile clipping ignores outliers."""
    depth = np.nan_to_num(depth.astype(np.float32), nan=0.0, posinf=0.0, neginf=0.0)
    lo, hi = np.percentile(depth, [0.5, 99.5])
    if hi - lo < 1e-8:
        return np.zeros_like(depth), float(lo), float(hi)
    return np.clip((depth - lo) / (hi - lo), 0.0, 1.0), float(lo), float(hi)


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------
def write_png(path: Path, image: np.ndarray) -> None:
    ok, buf = cv2.imencode(".png", image)
    if not ok:
        raise RuntimeError(f"Could not encode {path.name}")
    buf.tofile(str(path))  # tofile handles non-ASCII paths on Windows


def main() -> int:
    parser = argparse.ArgumentParser(description="Photo2Scene depth estimation")
    parser.add_argument("--input", required=True, help="JPG/PNG photo")
    parser.add_argument("--output-dir", required=True, help="Folder for results")
    parser.add_argument("--name", help="Output file prefix (default: input stem)")
    parser.add_argument("--model", choices=sorted(MODEL_IDS), default="small")
    parser.add_argument("--device", choices=["auto", "cpu", "cuda", "mps"], default="auto")
    parser.add_argument("--max-side", type=int, default=2048, help="Downscale larger photos")
    parser.add_argument("--dummy", action="store_true", help="Synthetic depth, no model")
    args = parser.parse_args()

    t_start = time.perf_counter()
    try:
        image_bgr = preprocess(read_image(Path(args.input)), args.max_side)
    except InputError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return EXIT_BAD_INPUT

    image_rgb = cv2.cvtColor(image_bgr, cv2.COLOR_BGR2RGB)
    try:
        if args.dummy:
            raw, info = estimate_depth_dummy(image_rgb)
        else:
            raw, info = estimate_depth_model(image_rgb, args.model, args.device)
    except ImportError as e:
        print(f"ERROR: Missing Python package ({e.name}). Run: pip install -r ai/requirements.txt",
              file=sys.stderr)
        return EXIT_MODEL_ERROR
    except Exception as e:  # noqa: BLE001 - report anything the model throws
        print(f"ERROR: Depth model failed: {type(e).__name__}: {e}", file=sys.stderr)
        return EXIT_MODEL_ERROR

    depth01, raw_min, raw_max = normalize(raw)

    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    name = args.name or Path(args.input).stem
    paths = {
        "rgb": out_dir / f"{name}_rgb.png",
        "depth16": out_dir / f"{name}_depth16.png",
        "depth_vis": out_dir / f"{name}_depth_vis.png",
        "meta": out_dir / f"{name}_meta.json",
    }
    try:
        write_png(paths["rgb"], image_bgr)
        write_png(paths["depth16"], np.round(depth01 * 65535.0).astype(np.uint16))
        vis = cv2.applyColorMap(np.round(depth01 * 255.0).astype(np.uint8), cv2.COLORMAP_INFERNO)
        write_png(paths["depth_vis"], vis)
    except Exception as e:  # noqa: BLE001
        print(f"ERROR: Could not write results: {e}", file=sys.stderr)
        return EXIT_MODEL_ERROR

    h, w = image_bgr.shape[:2]
    meta = {
        **info,
        "input": str(Path(args.input).resolve()),
        "width": w,
        "height": h,
        "convention": "depth16: 65535 = nearest, 0 = farthest (relative inverse depth)",
        "raw_min": raw_min,
        "raw_max": raw_max,
        "total_ms": round((time.perf_counter() - t_start) * 1000, 1),
        "outputs": {k: str(v) for k, v in paths.items()},
    }
    paths["meta"].write_text(json.dumps(meta, indent=2), encoding="utf-8")
    print(json.dumps({"ok": True, **{k: meta[k] for k in ("model", "device", "width", "height", "total_ms")}}))
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
