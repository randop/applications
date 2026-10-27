#!/usr/bin/env python3
"""
Pixel text art generator using mcer12/PixelFonts (7x5 thin)
Output style: ⏺️ on ⬛ background + 2-block padding
"""

import numpy as np
import urllib.request
import os

FONT_URL = "https://raw.githubusercontent.com/mcer12/PixelFonts/main/bdf/pixelfont_7x5_thin.bdf"
FONT_PATH = "/tmp/pixelfont_7x5_thin.bdf"
FONT_ASCENT = 7

def download_font():
    if not os.path.exists(FONT_PATH):
        print("Downloading font...")
        urllib.request.urlretrieve(FONT_URL, FONT_PATH)

def parse_bdf(path):
    glyphs = {}
    with open(path) as f:
        lines = f.readlines()

    i = 0
    while i < len(lines):
        line = lines[i].strip()
        if line.startswith("STARTCHAR"):
            encoding = None
            dwidth = 0
            bbx = None
            bitmap = []
            i += 1
            while i < len(lines):
                l = lines[i].strip()
                if l.startswith("ENCODING"):
                    encoding = int(l.split()[1])
                elif l.startswith("DWIDTH"):
                    dwidth = int(l.split()[1])
                elif l.startswith("BBX"):
                    bbx = list(map(int, l.split()[1:]))  # w, h, xoff, yoff
                elif l == "BITMAP":
                    i += 1
                    while i < len(lines) and not lines[i].strip().startswith("ENDCHAR"):
                        bitmap.append(lines[i].strip())
                        i += 1
                    break
                i += 1

            if encoding is not None and bbx is not None:
                w, h, xoff, yoff = bbx
                arr = np.zeros((h, w), dtype=bool)
                for row_idx, hexstr in enumerate(bitmap):
                    val = int(hexstr, 16)
                    bits = [(val >> (7 - b)) & 1 for b in range(8)]
                    for col in range(min(w, 8)):
                        if bits[col]:
                            arr[row_idx, col] = True
                glyphs[encoding] = {
                    "bitmap": arr,
                    "dwidth": dwidth,
                    "bbx": bbx,
                }
        i += 1
    return glyphs

def render_text(text: str, glyphs: dict, line_gap: int = 1) -> np.ndarray:
    lines = text.split("\n")
    rendered_lines = []
    max_w = 0

    for line in lines:
        width = sum(glyphs.get(ord(ch), {"dwidth": 3})["dwidth"] for ch in line)
        canvas = np.zeros((FONT_ASCENT, width), dtype=bool)
        x = 0

        for ch in line:
            g = glyphs.get(ord(ch))
            if g:
                bm = g["bitmap"]
                h, w = bm.shape
                xoff = g["bbx"][2]
                yoff = g["bbx"][3]
                y = FONT_ASCENT - h - yoff
                if y < 0:
                    y = 0
                canvas[y : y + h, x + xoff : x + xoff + w] |= bm
                x += g["dwidth"]
            else:
                x += 3

        # Crop empty space
        rows = np.any(canvas, axis=1)
        cols = np.any(canvas, axis=0)
        if np.any(rows):
            rmin, rmax = np.where(rows)[0][[0, -1]]
            cmin, cmax = np.where(cols)[0][[0, -1]]
            canvas = canvas[rmin : rmax + 1, cmin : cmax + 1]

        rendered_lines.append(canvas)
        max_w = max(max_w, canvas.shape[1])

    # Stack lines with gap
    padded = []
    for i, arr in enumerate(rendered_lines):
        h, w = arr.shape
        if w < max_w:
            pad = np.zeros((h, max_w - w), dtype=bool)
            arr = np.hstack([arr, pad])
        padded.append(arr)
        if i < len(rendered_lines) - 1:
            padded.append(np.zeros((line_gap, max_w), dtype=bool))

    full = np.vstack(padded)

    # Final crop
    rows = np.any(full, axis=1)
    cols = np.any(full, axis=0)
    rmin, rmax = np.where(rows)[0][[0, -1]]
    cmin, cmax = np.where(cols)[0][[0, -1]]
    return full[rmin : rmax + 1, cmin : cmax + 1]

def add_padding(arr: np.ndarray, pad: int = 2) -> np.ndarray:
    h, w = arr.shape
    padded = np.zeros((h + 2 * pad, w + 2 * pad), dtype=bool)
    padded[pad : pad + h, pad : pad + w] = arr
    return padded

def to_emoji(arr: np.ndarray, fill: str = "⏺️", empty: str = "⬛") -> str:
    return "\n".join("".join(fill if p else empty for p in row) for row in arr)

if __name__ == "__main__":
    download_font()
    glyphs = parse_bdf(FONT_PATH)

    text = "dhxpro"          # ← change this
    # text = "Super\nIntelligence"

    art = render_text(text, glyphs)
    art = add_padding(art, pad=2)
    print(to_emoji(art))
