#!/usr/bin/env python3
"""Generate Sabrina Tamagotchi-style sprites for the AiPi-Lite display.

Output format matches xiaozhi's existing emoji assets:
  * 32x32 RGB565A8 (RGB565 plane + 8-bit alpha plane = 3072 bytes per sprite)
  * lv_image_dsc_t static const declarations (one per frame)
  * Header file: main/assets/sprites/sabrina_sprites.h

Also writes 8x-scaled PNG previews to tools/sprite_previews/ so the
character can be visually inspected without flashing.

Run: python3 tools/gen_sprites.py
"""

from PIL import Image, ImageDraw
import os
import sys

W, H = 32, 32
SCALE = 8  # preview scale factor

# Character palette (RGB tuples). Background pixels (BG) get alpha=0;
# everything else gets alpha=255 for crisp edges.
#
# Inspired by Chibilunatchi (Tamagotchi fanon): pink body with cat ears
# + lopped rabbit ears, red eyes, blue dress with bow, red stockings.
BG      = (0,   0,   0)        # transparent
BODY    = (255, 175, 210)      # light pink (head + ears)
BODY_D  = (210, 130, 170)      # darker pink shading
OUTLINE = (90,  40,  80)       # dark purple-brown outline
WHITE   = (255, 255, 255)
BLACK   = (10,  10,  20)
BLUSH   = (255, 110, 160)
HAIR    = (255, 100, 150)      # forehead tuft + bow
HAIR_D  = (200, 60,  110)      # darker pink accents
EYE_R   = (220, 40,  70)       # red eyes
EYE_R_D = (140, 20,  40)       # red eye outline / inner shading
DRESS   = (110, 160, 220)      # mother's blue dress
DRESS_D = (60,  100, 170)      # dress shading / fold
STOCK   = (210, 50,  60)       # red stockings
STOCK_D = (140, 30,  40)       # stocking shading
HEART   = (240, 60,  100)      # heart center on the bow
ZZZ     = (160, 200, 255)      # light blue for sleep Z


def new_canvas():
    return Image.new("RGB", (W, H), BG)


# ----- Drawing helpers -----------------------------------------------------

def draw_body_base(d, hair_offset_y=0):
    """Chibilunatchi-style: pink head with cat ears (top) + lopped rabbit
    ears (sides hanging down), forehead tuft, blue dress, red stockings,
    bow with heart center.

    Layout (32x32):
      y 0-3   : cat ears (small triangles on top of head)
      y 1-7   : bow (between/over the cat ears)
      y 4-15  : head circle
      y 6-22  : lopped rabbit ears (sides, hanging + folded)
      y 7-9   : forehead tuft
      y 14-25 : blue dress (flared)
      y 25-29 : red stockings + feet
    """
    # 1) Cat ears (small triangles — drawn first so bow overlaps inner edges)
    # Left cat ear
    d.polygon([(7, 5), (12, 5), (10, 0)], fill=BODY, outline=OUTLINE)
    d.point((10, 3), fill=HAIR)  # inner ear pink
    # Right cat ear
    d.polygon([(19, 5), (24, 5), (21, 0)], fill=BODY, outline=OUTLINE)
    d.point((21, 3), fill=HAIR)

    # 2) Lopped rabbit ears (long pink shapes hanging on each side, with
    # a fold/lop near the bottom suggesting floppy ears)
    # Left rabbit ear: top straight, bottom folds outward
    d.ellipse([1,  6,  5,  14], fill=BODY,   outline=OUTLINE)   # top
    d.ellipse([0,  12, 6,  20], fill=BODY,   outline=OUTLINE)   # lopped tip
    d.point((3, 9),  fill=HAIR)   # inner ear shading top
    d.point((3, 16), fill=HAIR_D) # inner ear shading tip
    # Right rabbit ear
    d.ellipse([26, 6,  30, 14], fill=BODY,   outline=OUTLINE)
    d.ellipse([25, 12, 31, 20], fill=BODY,   outline=OUTLINE)
    d.point((28, 9),  fill=HAIR)
    d.point((28, 16), fill=HAIR_D)

    # 3) Blue dress — flared trapezoid below the head
    dress_poly = [(10, 14), (21, 14), (26, 25), (5, 25)]
    d.polygon(dress_poly, fill=DRESS, outline=OUTLINE)
    # Dress fold highlights
    d.line([(13, 16), (10, 25)], fill=DRESS_D)
    d.line([(16, 16), (16, 25)], fill=DRESS_D)
    d.line([(18, 16), (21, 25)], fill=DRESS_D)
    # Dress collar accent (small bow detail near neck)
    d.point((15, 14), fill=HEART)
    d.point((16, 14), fill=HEART)

    # 4) Head — sits on top of the dress
    d.ellipse([8, 4, 23, 16], fill=BODY, outline=OUTLINE)
    # Lower-right shading
    d.ellipse([16, 11, 22, 15], fill=BODY_D, outline=None)
    d.ellipse([8, 4, 23, 16], fill=None, outline=OUTLINE)

    # 5) Forehead tuft (signature feature inherited from her father —
    # small pink hair points just above the eyes).
    d.polygon([(12, 6), (15, 6), (14, 9), (13, 8)], fill=HAIR,   outline=OUTLINE)
    d.polygon([(16, 6), (19, 6), (18, 9), (17, 8)], fill=HAIR_D, outline=OUTLINE)

    # 6) Bow on top of head with heart center
    by = 1 + hair_offset_y
    d.ellipse([10, by,     14, by + 5], fill=HAIR,   outline=OUTLINE)
    d.ellipse([17, by,     21, by + 5], fill=HAIR,   outline=OUTLINE)
    d.rectangle([(14, by + 1), (17, by + 4)], fill=HAIR_D, outline=OUTLINE)
    # Heart in center of bow (5-pixel approximation)
    d.point((15, by + 2), fill=HEART)
    d.point((16, by + 2), fill=HEART)
    d.point((15, by + 3), fill=HEART)
    d.point((16, by + 3), fill=HEART)
    d.point((15, by + 4), fill=HEART)
    # Decorative ribbon dangles below the bow
    d.line([(13, by + 5), (12, by + 7)], fill=HAIR_D)
    d.line([(18, by + 5), (19, by + 7)], fill=HAIR_D)


def draw_eyelashes(d):
    """Outer-corner lashes — small tick marks above outer eye corners."""
    d.point((9,  9),  fill=BLACK)
    d.point((10, 9),  fill=BLACK)
    d.point((22, 9),  fill=BLACK)
    d.point((21, 9),  fill=BLACK)


def draw_eyes_open(d):
    """Big red eyes (Chibilunatchi signature) — fits within head y=4..16."""
    # Eye whites
    d.ellipse([10, 10, 13, 13], fill=WHITE, outline=BLACK)
    d.ellipse([18, 10, 21, 13], fill=WHITE, outline=BLACK)
    # Red iris/pupil
    d.ellipse([11, 11, 12, 12], fill=EYE_R,  outline=EYE_R_D)
    d.ellipse([19, 11, 20, 12], fill=EYE_R,  outline=EYE_R_D)
    # Highlight sparkles
    d.point((12, 11), fill=WHITE)
    d.point((20, 11), fill=WHITE)
    draw_eyelashes(d)


def draw_eyes_closed_blink(d):
    """Eyes closed flat — blinking."""
    d.line([(10, 11), (13, 11)], fill=BLACK)
    d.line([(18, 11), (21, 11)], fill=BLACK)
    draw_eyelashes(d)


def draw_eyes_squint_happy(d):
    """Eyes ^ ^ — happy squint."""
    d.line([(10, 12), (11, 10)], fill=BLACK)
    d.line([(11, 10), (13, 12)], fill=BLACK)
    d.line([(18, 12), (19, 10)], fill=BLACK)
    d.line([(19, 10), (21, 12)], fill=BLACK)
    draw_eyelashes(d)


def draw_eyes_sleep(d):
    """Eyes closed downward curve — sleeping."""
    for x_off in (10, 18):
        d.line([(x_off, 11), (x_off + 1, 12), (x_off + 2, 12), (x_off + 3, 11)], fill=BLACK)


def draw_smile(d):
    """Small upward smile, fits inside the head."""
    d.line([(13, 14), (14, 15), (17, 15), (18, 14)], fill=OUTLINE)


def draw_big_smile(d):
    """Bigger open smile (no overflow into dress)."""
    d.line([(12, 14), (13, 15), (18, 15), (19, 14)], fill=OUTLINE)
    d.line([(13, 15), (18, 15)], fill=OUTLINE)


def draw_yawn(d):
    """Tiny 'o' for sleeping mouth."""
    d.point((15, 14), fill=OUTLINE)
    d.point((16, 14), fill=OUTLINE)
    d.point((15, 15), fill=OUTLINE)
    d.point((16, 15), fill=OUTLINE)


def draw_blush(d):
    """Pink cheeks just below the eyes."""
    d.point((9,  13), fill=BLUSH)
    d.point((22, 13), fill=BLUSH)
    d.point((9,  14), fill=BLUSH)
    d.point((22, 14), fill=BLUSH)


def draw_feet(d, left_y=0, right_y=0):
    """Red stockings + dark shoes — Chibilunatchi's signature legwear."""
    # Left leg (stocking + shoe)
    ly = 26 - left_y
    d.rectangle([(11, ly), (13, ly + 2)], fill=STOCK,   outline=STOCK_D)
    d.point((12, ly + 3), fill=OUTLINE)  # shoe peeks below
    # Right leg
    ry = 26 - right_y
    d.rectangle([(18, ry), (20, ry + 2)], fill=STOCK,   outline=STOCK_D)
    d.point((19, ry + 3), fill=OUTLINE)


def draw_z_floating(d):
    """Sleep 'Z' floating up-right of the head."""
    # Big Z (clear of pigtail)
    d.line([(26, 2), (30, 2)], fill=ZZZ)
    d.line([(30, 2), (26, 5)], fill=ZZZ)
    d.line([(26, 5), (30, 5)], fill=ZZZ)


# ----- Frame builders ------------------------------------------------------

def make_idle_open():
    img = new_canvas(); d = ImageDraw.Draw(img)
    draw_body_base(d)
    draw_eyes_open(d)
    draw_blush(d)
    draw_smile(d)
    draw_feet(d)
    return img


def make_idle_blink():
    img = new_canvas(); d = ImageDraw.Draw(img)
    draw_body_base(d)
    draw_eyes_closed_blink(d)
    draw_blush(d)
    draw_smile(d)
    draw_feet(d)
    return img


def make_walk_left():
    img = new_canvas(); d = ImageDraw.Draw(img)
    draw_body_base(d, hair_offset_y=-1)
    draw_eyes_open(d)
    draw_blush(d)
    draw_smile(d)
    draw_feet(d, left_y=2, right_y=0)
    return img


def make_walk_right():
    img = new_canvas(); d = ImageDraw.Draw(img)
    draw_body_base(d, hair_offset_y=-1)
    draw_eyes_open(d)
    draw_blush(d)
    draw_smile(d)
    draw_feet(d, left_y=0, right_y=2)
    return img


def make_happy():
    img = new_canvas(); d = ImageDraw.Draw(img)
    draw_body_base(d)
    draw_eyes_squint_happy(d)
    # Bigger blush
    d.ellipse([8, 17, 11, 20],  fill=BLUSH)
    d.ellipse([20, 17, 23, 20], fill=BLUSH)
    draw_big_smile(d)
    draw_feet(d)
    return img


def make_sleep():
    img = new_canvas(); d = ImageDraw.Draw(img)
    draw_body_base(d)
    draw_eyes_sleep(d)
    draw_blush(d)
    draw_yawn(d)
    draw_feet(d)
    draw_z_floating(d)
    return img


SPRITES = {
    "sabrina_idle_open":  make_idle_open,
    "sabrina_idle_blink": make_idle_blink,
    "sabrina_walk_left":  make_walk_left,
    "sabrina_walk_right": make_walk_right,
    "sabrina_happy":      make_happy,
    "sabrina_sleep":      make_sleep,
}


# ----- RGB565A8 conversion -------------------------------------------------

def rgb_to_rgb565(rgb):
    r, g, b = rgb
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def img_to_rgb565a8_bytes(img):
    """RGB565 plane (W*H*2 bytes) + alpha plane (W*H bytes).

    Pixels matching BG become fully transparent; everything else fully opaque.
    """
    pixels = list(img.getdata())
    rgb_data = bytearray()
    alpha_data = bytearray()
    for r, g, b in pixels:
        rgb565 = rgb_to_rgb565((r, g, b))
        rgb_data.append(rgb565 & 0xFF)
        rgb_data.append((rgb565 >> 8) & 0xFF)
        alpha_data.append(0x00 if (r, g, b) == BG else 0xFF)
    return bytes(rgb_data) + bytes(alpha_data)


def emit_c_array(name, data):
    """Emit a `static const uint8_t name_map[]` array + a `const
    lv_image_dsc_t name` descriptor. Uses C++-compatible aggregate
    initializer (nested designated initializers don't work in C++)."""
    lines = [f"static const uint8_t {name}_map[] = {{"]
    for i in range(0, len(data), 32):
        chunk = data[i:i + 32]
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    lines.append("};")
    lines.append("")
    lines.append(f"const lv_image_dsc_t {name} = {{")
    lines.append("    .header = {")
    lines.append("        .magic = LV_IMAGE_HEADER_MAGIC,")
    lines.append("        .cf = LV_COLOR_FORMAT_RGB565A8,")
    lines.append("        .flags = 0,")
    lines.append(f"        .w = {W},")
    lines.append(f"        .h = {H},")
    lines.append(f"        .stride = {W * 2},")
    lines.append("    },")
    lines.append(f"    .data_size = sizeof({name}_map),")
    lines.append(f"    .data = {name}_map,")
    lines.append("};")
    return "\n".join(lines)


# ----- Main ----------------------------------------------------------------

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.abspath(os.path.join(here, ".."))

    preview_dir = os.path.join(here, "sprite_previews")
    os.makedirs(preview_dir, exist_ok=True)

    out_dir = os.path.join(repo, "main", "assets", "sprites")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, "sabrina_sprites.h")

    header_lines = [
        "// AUTOGENERATED by tools/gen_sprites.py — do not edit by hand.",
        "// Run `python3 tools/gen_sprites.py` to regenerate.",
        "//",
        "// Sabrina Tamagotchi sprites: 32x32 RGB565A8.",
        "// Each sprite is 32*32*2 (RGB565) + 32*32*1 (alpha) = 3072 bytes.",
        "#pragma once",
        "",
        '#include "lvgl.h"',
        "",
    ]

    for name, fn in SPRITES.items():
        img = fn()
        # Save 8x preview
        scaled = img.resize((W * SCALE, H * SCALE), Image.NEAREST)
        scaled.save(os.path.join(preview_dir, f"{name}.png"))
        # Append C array
        data = img_to_rgb565a8_bytes(img)
        assert len(data) == W * H * 3, f"bad data length: {len(data)}"
        header_lines.append(emit_c_array(name, data))
        header_lines.append("")

    with open(out_path, "w") as f:
        f.write("\n".join(header_lines))

    print(f"Generated {len(SPRITES)} sprites:")
    print(f"  Header:   {out_path}")
    print(f"  Previews: {preview_dir}/*.png")
    print(f"  Per-sprite size: {W * H * 3} bytes")


if __name__ == "__main__":
    main()
