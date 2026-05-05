#!/usr/bin/env python3
"""Render a WFC input sample as a scaled PNG for documentation."""
import sys
from pathlib import Path

PALETTE = [
    (0, 0, 0),         (255, 255, 255), (31, 119, 180), (255, 127, 14),
    (44, 160, 44),     (148, 103, 189), (214, 39, 40),  (140, 86, 75),
    (227, 119, 194),   (127, 127, 127), (188, 189, 34), (23, 190, 207),
    (174, 199, 232),   (255, 187, 120), (152, 223, 138), (197, 176, 213),
]


def read_grid(path: Path):
    rows = []
    for line in path.read_text().splitlines():
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        rows.append([int(v) for v in s.split()])
    return rows


def write_ppm(path: Path, rows, scale: int = 12):
    if not rows:
        return
    height = len(rows) * scale
    width = len(rows[0]) * scale
    with path.open("wb") as out:
        out.write(f"P6\n{width} {height}\n255\n".encode())
        for r in rows:
            line = b""
            for v in r:
                color = PALETTE[v % 16]
                line += bytes(color) * scale
            for _ in range(scale):
                out.write(line)


def write_png(path: Path, rows, scale: int = 16):
    if not rows:
        return
    try:
        from PIL import Image
    except ImportError:
        return
    height = len(rows) * scale
    width = len(rows[0]) * scale
    img = Image.new("RGB", (width, height))
    pixels = img.load()
    for r_idx, r in enumerate(rows):
        for c_idx, v in enumerate(r):
            color = PALETTE[v % 16]
            for dr in range(scale):
                for dc in range(scale):
                    pixels[c_idx * scale + dc, r_idx * scale + dr] = color
    img.save(path, optimize=True)


def main():
    out_dir = Path("docs/figures/results/inputs")
    out_dir.mkdir(parents=True, exist_ok=True)
    for arg in sys.argv[1:]:
        sample = Path(arg)
        rows = read_grid(sample)
        out = out_dir / (sample.stem + "_input.png")
        write_png(out, rows, scale=16)
        print(f"wrote {out}  ({len(rows)}x{len(rows[0]) if rows else 0})")


if __name__ == "__main__":
    main()
