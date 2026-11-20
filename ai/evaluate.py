#!/usr/bin/env python3
"""Photo2Scene evaluation helpers (Experiments 1, 5).

PSNR / SSIM between a reference image and a render. Only meaningful when the two
images show the same view, e.g. the app's "Export original-view renders":

    python evaluate.py ../outputs/photo_reference.png ../outputs/photo_render_unlit.png
    python evaluate.py ../outputs/photo_reference.png ../outputs/photo_render_*.png --csv results.csv

Note: these compare 2D images. They are NOT a direct measure of 3D accuracy.
"""
from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

import cv2
import numpy as np


def load_gray_float(path: Path, size: tuple[int, int] | None = None) -> np.ndarray:
    data = np.fromfile(str(path), dtype=np.uint8)
    img = cv2.imdecode(data, cv2.IMREAD_COLOR)
    if img is None:
        raise SystemExit(f"Cannot read {path}")
    if size is not None and (img.shape[1], img.shape[0]) != size:
        img = cv2.resize(img, size, interpolation=cv2.INTER_AREA)
    return img.astype(np.float64)


def psnr(a: np.ndarray, b: np.ndarray) -> float:
    mse = np.mean((a - b) ** 2)
    return float("inf") if mse == 0 else 10.0 * np.log10(255.0 ** 2 / mse)


def ssim(a: np.ndarray, b: np.ndarray) -> float:
    """Mean SSIM (Wang et al. 2004), Gaussian window sigma=1.5, on luminance."""
    to_y = lambda im: cv2.cvtColor(im.astype(np.float32), cv2.COLOR_BGR2GRAY).astype(np.float64)
    x, y = to_y(a), to_y(b)
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2
    blur = lambda im: cv2.GaussianBlur(im, (11, 11), 1.5)
    mu_x, mu_y = blur(x), blur(y)
    sxx = blur(x * x) - mu_x ** 2
    syy = blur(y * y) - mu_y ** 2
    sxy = blur(x * y) - mu_x * mu_y
    num = (2 * mu_x * mu_y + c1) * (2 * sxy + c2)
    den = (mu_x ** 2 + mu_y ** 2 + c1) * (sxx + syy + c2)
    return float(np.mean(num / den))


def main() -> int:
    p = argparse.ArgumentParser(description="PSNR/SSIM against a reference image")
    p.add_argument("reference")
    p.add_argument("images", nargs="+")
    p.add_argument("--csv", help="Append results to this CSV file")
    args = p.parse_args()

    ref_path = Path(args.reference)
    ref = load_gray_float(ref_path)
    size = (ref.shape[1], ref.shape[0])

    rows = []
    print(f"{'image':40s} {'PSNR (dB)':>10s} {'SSIM':>8s}")
    for name in args.images:
        img = load_gray_float(Path(name), size)
        row = {"reference": ref_path.name, "image": Path(name).name,
               "psnr_db": round(psnr(ref, img), 3), "ssim": round(ssim(ref, img), 4)}
        rows.append(row)
        print(f"{row['image']:40s} {row['psnr_db']:>10.2f} {row['ssim']:>8.4f}")

    if args.csv:
        path = Path(args.csv)
        new = not path.exists()
        with path.open("a", newline="", encoding="utf-8") as f:
            writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
            if new:
                writer.writeheader()
            writer.writerows(rows)
    return 0


if __name__ == "__main__":
    sys.exit(main())
