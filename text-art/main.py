from PIL import Image, ImageDraw, ImageFont
import numpy as np

font_path = "/usr/share/fonts/SlidesCarnival/google/JetBrains Mono/static/JetBrainsMono-Regular.ttf"
font_size = 128
font = ImageFont.truetype(font_path, font_size)

text = "mail"
bbox = font.getbbox(text)
pad = 16
width = bbox[2] - bbox[0] + 2 * pad
height = bbox[3] - bbox[1] + 2 * pad

img = Image.new('L', (width, height), 255)
draw = ImageDraw.Draw(img)
draw.text((pad - bbox[0], pad - bbox[1]), text, font=font, fill=0)

arr = np.array(img)
binary = arr < 100

rows = np.any(binary, axis=1)
cols = np.any(binary, axis=0)
rmin, rmax = np.where(rows)[0][[0, -1]]
cmin, cmax = np.where(cols)[0][[0, -1]]
cropped = binary[rmin:rmax+1, cmin:cmax+1]

target_h = 12
h, w = cropped.shape
scale = h / target_h
target_w = int(round(w / scale))

bin_img = Image.fromarray((cropped.astype(np.uint8) * 255))
resized = bin_img.resize((target_w, target_h), Image.NEAREST)
res_arr = np.array(resized) > 128

col_has = np.any(res_arr, axis=0)
cstart = np.argmax(col_has)
cend = len(col_has) - np.argmax(col_has[::-1])
res_arr = res_arr[:, cstart:cend]

for row in res_arr:
    print("".join("🟩" if p else "⬛" for p in row))
