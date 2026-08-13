# A4 Month Sticker Sheet Generator

Generates a printable A4 PDF sticker sheet with a grid of small boxes, each
showing a status label (`OK` or `BAD`), a year, and a 3-letter month
abbreviation. Designed for sticker paper — 0.25" margins, 0.25" x 0.5" boxes,
thin black borders, fit to a single page.

## 1. Requirements

- Python 3.7+
- pip

Check your Python version:

```bash
python3 --version
```

## 2. Install dependencies

Only one dependency is needed: **reportlab**.

**Linux / macOS:**

```bash
pip install reportlab --break-system-packages
```

> `--break-system-packages` is needed on newer distros (e.g. Debian/Ubuntu
> 23.04+, Arch/Artix) that block global pip installs. If you're using a
> virtual environment, you can omit that flag.

**Using a virtual environment instead (optional, cleaner):**

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install reportlab
```

**Windows:**

```bash
pip install reportlab
```

## 3. Files

| File | Purpose |
|---|---|
| `make_stickers_ok.py` | Generates the **OK** sticker sheet (green label, white text) |
| `make_stickers_bad.py` | Generates the **BAD** sticker sheet (yellow label, black text) |

Both scripts are self-contained — no shared modules to install.

## 4. Run

From the folder containing the scripts:

```bash
python3 make_stickers_ok.py
python3 make_stickers_bad.py
```

Each run prints a summary and writes a PDF to the same directory:

```
Rows: 22 Cols: 31 Total boxes: 682
Grid width used (in): 7.75
Grid height used (in): 11.0
```

Output files:
- `stickers_2027_ok.pdf`
- `stickers_2027_bad.pdf`

(exact filenames depend on the `OUTPUT_FILE` setting inside each script —
see below).

## 5. Customize

Open either script and edit the config block near the top:

```python
LABEL = "OK"                      # text shown in the top strip
LABEL_BG = HexColor("#1e8e3e")    # top strip background color
LABEL_FG = white                  # top strip text color
YEAR = "2027"                     # middle strip text
OUTPUT_FILE = "stickers_2027_ok.pdf"
```

Common tweaks:
- **Change the year** — edit `YEAR`.
- **Change the label text** — edit `LABEL` (keep it short, 2–3 letters fits
  best in the 0.25" wide box).
- **Change colors** — edit `LABEL_BG` / `LABEL_FG` using a hex color, e.g.
  `HexColor("#d32f2f")` for red, or use `black` / `white` (already imported).
- **Change box/margin size** — edit `MARGIN`, `BOX_W`, `BOX_H` near the top
  (values are in inches, multiplied by 72 to convert to points).

After editing, just re-run the script — it regenerates the PDF.

## 6. Printing

- Paper size: **A4**
- Margins: printer should use **actual size / 100%** scaling (not
  "fit to page"), otherwise the 0.25" margins and box sizes will shift.
- Recommended: print a single test page on plain paper first to check
  alignment before using sticker paper.

## 7. Troubleshooting

- **`ModuleNotFoundError: No module named 'reportlab'`** — the install step
  was skipped or installed into a different Python environment. Re-run the
  `pip install` command using the same `python3` you use to run the script.
- **`error: externally-managed-environment`** — add `--break-system-packages`
  to the `pip install` command, or use a virtual environment.
- **Text looks too small when printed** — this is expected at this box size
  (0.25" x 0.5"); it's designed for close-up label reading, not readability
  from a distance. Increase `BOX_W` / `BOX_H` if you need larger stickers.

