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
BG      = (0,   0,   0)        # transparent
BODY    = (255, 175, 210)      # light pink
BODY_D  = (210, 130, 170)      # darker pink (shading)
OUTLINE = (90,  40,  80)       # dark purple-brown outline
WHITE   = (255, 255, 255)
BLACK   = (10,  10,  20)
BLUSH   = (255, 110, 160)
HAIR    = (255, 100, 150)      # bow + hair tuft
HAIR_D  = (200, 60,  110)
ZZZ     = (160, 200, 255)      # light blue for sleep Z


def new_canvas():
    return Image.new("RGB", (W, H), BG)


# ----- Drawing helpers -----------------------------------------------------

def draw_body_base(d, hair_offset_y=0):
    """Round body + hair bow on top. Sets up the shape both idle/walk/etc share."""
    # Body: slightly wider than tall, centered low so feet poke below
    d.ellipse([6, 8, 25, 26], fill=BODY, outline=OUTLINE)
    # Body shading on lower-right (gives 3D feel)
    d.ellipse([14, 16, 24, 25], fill=BODY_D, outline=None)
    d.ellipse([6, 8, 25, 26], fill=None, outline=OUTLINE)
    # Hair tuft on top (big bow)
    by = 2 + hair_offset_y
    d.ellipse([10, by, 14, by + 5], fill=HAIR, outline=OUTLINE)
    d.ellipse([17, by, 21, by + 5], fill=HAIR, outline=OUTLINE)
    d.rectangle([(14, by + 1), (17, by + 4)], fill=HAIR_D, outline=OUTLINE)
    d.point((15, by + 2), fill=WHITE)


def draw_eyes_open(d):
    """Big anime-style eyes."""
    # Whites
    d.ellipse([10, 12, 14, 17], fill=WHITE, outline=BLACK)
    d.ellipse([17, 12, 21, 17], fill=WHITE, outline=BLACK)
    # Pupils
    d.ellipse([11, 13, 13, 16], fill=BLACK, outline=None)
    d.ellipse([18, 13, 20, 16], fill=BLACK, outline=None)
    # Highlight (gives the eyes life)
    d.point((12, 13), fill=WHITE)
    d.point((19, 13), fill=WHITE)


def draw_eyes_closed_blink(d):
    """Eyes closed flat — blinking."""
    d.line([(10, 14), (14, 14)], fill=BLACK)
    d.line([(17, 14), (21, 14)], fill=BLACK)


def draw_eyes_squint_happy(d):
    """Eyes ^ ^ — happy squint."""
    d.line([(10, 15), (12, 13)], fill=BLACK)
    d.line([(12, 13), (14, 15)], fill=BLACK)
    d.line([(17, 15), (19, 13)], fill=BLACK)
    d.line([(19, 13), (21, 15)], fill=BLACK)


def draw_eyes_sleep(d):
    """Eyes closed downward curve — sleeping."""
    # Two arcs (drawn as short curved lines via points)
    for x_off in (10, 17):
        d.line([(x_off, 14), (x_off + 1, 15), (x_off + 2, 15), (x_off + 3, 14)], fill=BLACK)


def draw_smile(d):
    """Small upward smile."""
    d.line([(13, 19), (14, 20), (17, 20), (18, 19)], fill=OUTLINE)


def draw_big_smile(d):
    """Bigger open smile."""
    d.line([(12, 19), (13, 21), (18, 21), (19, 19)], fill=OUTLINE)
    d.line([(13, 21), (18, 21)], fill=OUTLINE)


def draw_yawn(d):
    """Tiny 'o' for sleeping mouth."""
    d.point((15, 20), fill=OUTLINE)
    d.point((16, 20), fill=OUTLINE)
    d.point((15, 21), fill=OUTLINE)
    d.point((16, 21), fill=OUTLINE)


def draw_blush(d):
    """Pink cheeks."""
    d.point((9, 18), fill=BLUSH)
    d.point((22, 18), fill=BLUSH)
    d.point((9, 19), fill=BLUSH)
    d.point((22, 19), fill=BLUSH)


def draw_feet(d, left_y=0, right_y=0):
    """Two feet poking below the body. left_y/right_y shift each foot up by N px."""
    d.rectangle([(11, 25 - left_y),  (13, 27 - left_y)],  fill=OUTLINE)
    d.rectangle([(18, 25 - right_y), (20, 27 - right_y)], fill=OUTLINE)


def draw_z_floating(d):
    """Sleep 'Z' floating up-right."""
    # Big Z
    d.line([(24, 4), (28, 4)], fill=ZZZ)
    d.line([(28, 4), (24, 7)], fill=ZZZ)
    d.line([(24, 7), (28, 7)], fill=ZZZ)
    # Small z
    d.line([(22, 9), (24, 9)], fill=ZZZ)
    d.line([(24, 9), (22, 11)], fill=ZZZ)
    d.line([(22, 11), (24, 11)], fill=ZZZ)


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
    lines = [f"static const uint8_t {name}_map[] = {{"]
    for i in range(0, len(data), 32):
        chunk = data[i:i + 32]
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    lines.append("};")
    lines.append("")
    lines.append(f"const lv_image_dsc_t {name} = {{")
    lines.append("    .header.magic = LV_IMAGE_HEADER_MAGIC,")
    lines.append("    .header.cf = LV_COLOR_FORMAT_RGB565A8,")
    lines.append("    .header.flags = 0,")
    lines.append(f"    .header.w = {W},")
    lines.append(f"    .header.h = {H},")
    lines.append(f"    .header.stride = {W * 2},")
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
