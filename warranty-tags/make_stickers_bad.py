"""
A4 sticker sheet generator - BAD / 2027 / MONTH labels.
Requires: pip install reportlab
"""
from reportlab.lib.pagesizes import A4
from reportlab.pdfgen import canvas
from reportlab.lib.colors import HexColor, black, white

# ---- Config (edit these) ----
LABEL = "BAD"
LABEL_BG = HexColor("#ffd400")   # yellow
LABEL_FG = black
YEAR = "2027"
OUTPUT_FILE = "stickers_2027_bad.pdf"

MARGIN = 0.25 * 72          # 0.25 inch margin
BOX_W = 0.25 * 72           # 0.25 inch box width
BOX_H = 0.5 * 72            # 0.5 inch box height

MONTHS = ["JAN", "FEB", "MAR", "APR", "MAY", "JUN",
          "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"]

page_w, page_h = A4

usable_w = page_w - 2 * MARGIN
usable_h = page_h - 2 * MARGIN
COLS = int(usable_w // BOX_W)   # fill full page width (repeats Jan-Dec band)
ROWS = int(usable_h // BOX_H)

start_x = MARGIN
start_y = page_h - MARGIN

c = canvas.Canvas(OUTPUT_FILE, pagesize=A4)
c.setLineWidth(0.4)  # thin black border

strip_h = BOX_H / 3.0

for r in range(ROWS):
    y_top = start_y - r * BOX_H
    for col in range(COLS):
        x_left = start_x + col * BOX_W
        month = MONTHS[col % 12]

        # --- Strip 1 (top): LABEL, colored bg ---
        y1_bot = y_top - strip_h
        c.setFillColor(LABEL_BG)
        c.rect(x_left, y1_bot, BOX_W, strip_h, stroke=0, fill=1)
        c.setFillColor(LABEL_FG)
        c.setFont("Helvetica-Bold", 6.5)
        tw = c.stringWidth(LABEL, "Helvetica-Bold", 6.5)
        c.drawString(x_left + (BOX_W - tw) / 2.0,
                     y1_bot + (strip_h - 6.5) / 2.0 + 1.3, LABEL)

        # --- Strip 2 (middle): YEAR, black text, white bg ---
        y2_bot = y1_bot - strip_h
        c.setFillColor(white)
        c.rect(x_left, y2_bot, BOX_W, strip_h, stroke=0, fill=1)
        c.setFillColor(black)
        c.setFont("Helvetica", 6)
        tw = c.stringWidth(YEAR, "Helvetica", 6)
        c.drawString(x_left + (BOX_W - tw) / 2.0,
                     y2_bot + (strip_h - 6) / 2.0 + 1.2, YEAR)

        # --- Strip 3 (bottom): month abbrev, black text, white bg ---
        y3_bot = y2_bot - strip_h
        c.setFillColor(white)
        c.rect(x_left, y3_bot, BOX_W, strip_h, stroke=0, fill=1)
        c.setFillColor(black)
        c.setFont("Helvetica-Bold", 6)
        tw = c.stringWidth(month, "Helvetica-Bold", 6)
        c.drawString(x_left + (BOX_W - tw) / 2.0,
                     y3_bot + (strip_h - 6) / 2.0 + 1.2, month)

        # --- Outer box border (thin black) ---
        c.setStrokeColor(black)
        c.rect(x_left, y_top - BOX_H, BOX_W, BOX_H, stroke=1, fill=0)

c.save()
print("Rows:", ROWS, "Cols:", COLS, "Total boxes:", ROWS * COLS)
print("Grid width used (in):", (COLS * BOX_W) / 72.0)
print("Grid height used (in):", (ROWS * BOX_H) / 72.0)
