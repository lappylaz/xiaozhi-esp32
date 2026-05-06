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
    """Head + flared skirt-body + pigtails + bow.

    Layout (32x32):
      y 1-6   : bow
      y 4-15  : head circle
      y 6-19  : pigtails (sides)
      y 14-26 : skirt-shaped body, flared at bottom
      y 25-28 : feet (drawn separately by draw_feet)
    """
    # 1) Pigtails (drawn first so head overlaps their inside edges)
    # Left pigtail: vertical oblong, darker tip at bottom
    d.ellipse([1,  7,  7,  19], fill=HAIR,   outline=OUTLINE)
    d.ellipse([1,  15, 6,  19], fill=HAIR_D, outline=OUTLINE)
    # Right pigtail
    d.ellipse([24, 7,  30, 19], fill=HAIR,   outline=OUTLINE)
    d.ellipse([25, 15, 30, 19], fill=HAIR_D, outline=OUTLINE)
    # Hair-tie band where pigtails meet head (small darker accent)
    d.line([(5, 8),  (7, 8)],   fill=HAIR_D)
    d.line([(24, 8), (26, 8)],  fill=HAIR_D)

    # 2) Skirt — flared trapezoid below the head (drawn before head so the
    # head overlaps the top edge cleanly).
    skirt_poly = [(10, 14), (21, 14), (26, 25), (5, 25)]
    d.polygon(skirt_poly, fill=BODY_D, outline=OUTLINE)
    # Skirt vertical fold highlights
    d.line([(13, 16), (10, 25)], fill=BODY)
    d.line([(16, 16), (16, 25)], fill=BODY)
    d.line([(18, 16), (21, 25)], fill=BODY)

    # 3) Head — round, sits on top of the skirt
    d.ellipse([8, 4, 23, 16], fill=BODY, outline=OUTLINE)
    # Light shading on lower right of head
    d.ellipse([16, 11, 22, 15], fill=BODY_D, outline=None)
    d.ellipse([8, 4, 23, 16], fill=None, outline=OUTLINE)

    # 4) Bow on top of head
    by = 1 + hair_offset_y
    d.ellipse([10, by,     14, by + 5], fill=HAIR,   outline=OUTLINE)
    d.ellipse([17, by,     21, by + 5], fill=HAIR,   outline=OUTLINE)
    d.rectangle([(14, by + 1), (17, by + 4)], fill=HAIR_D, outline=OUTLINE)
    # Tiny heart sparkle in bow center
    d.point((15, by + 2), fill=WHITE)
    d.point((16, by + 2), fill=WHITE)
    d.point((15, by + 3), fill=WHITE)


def draw_eyelashes(d):
    """Outer-corner lashes — small tick marks above outer eye edges."""
    # Left eye outer corner (on the LEFT side of left eye)
    d.point((9,  10), fill=BLACK)
    d.point((10, 10), fill=BLACK)
    # Right eye outer corner (on the RIGHT side of right eye)
    d.point((22, 10), fill=BLACK)
    d.point((21, 10), fill=BLACK)


def draw_eyes_open(d):
    """Big anime-style eyes (positioned within head y=4..16)."""
    # Whites
    d.ellipse([10,  8, 13, 12], fill=WHITE, outline=BLACK)
    d.ellipse([18,  8, 21, 12], fill=WHITE, outline=BLACK)
    # Pupils
    d.ellipse([11,  9, 12, 11], fill=BLACK, outline=None)
    d.ellipse([19,  9, 20, 11], fill=BLACK, outline=None)
    # Highlight sparkles
    d.point((12,  9), fill=WHITE)
    d.point((20,  9), fill=WHITE)
    draw_eyelashes(d)


def draw_eyes_closed_blink(d):
    """Eyes closed flat — blinking."""
    d.line([(10, 10), (13, 10)], fill=BLACK)
    d.line([(18, 10), (21, 10)], fill=BLACK)
    draw_eyelashes(d)


def draw_eyes_squint_happy(d):
    """Eyes ^ ^ — happy squint."""
    d.line([(10, 11), (11,  9)], fill=BLACK)
    d.line([(11,  9), (13, 11)], fill=BLACK)
    d.line([(18, 11), (19,  9)], fill=BLACK)
    d.line([(19,  9), (21, 11)], fill=BLACK)
    draw_eyelashes(d)


def draw_eyes_sleep(d):
    """Eyes closed downward curve — sleeping."""
    for x_off in (10, 18):
        d.line([(x_off, 10), (x_off + 1, 11), (x_off + 2, 11), (x_off + 3, 10)], fill=BLACK)


def draw_smile(d):
    """Small upward smile, fits inside the head."""
    d.line([(13, 13), (14, 14), (17, 14), (18, 13)], fill=OUTLINE)


def draw_big_smile(d):
    """Bigger open smile."""
    d.line([(12, 13), (13, 15), (18, 15), (19, 13)], fill=OUTLINE)
    d.line([(13, 15), (18, 15)], fill=OUTLINE)


def draw_yawn(d):
    """Tiny 'o' for sleeping mouth."""
    d.point((15, 14), fill=OUTLINE)
    d.point((16, 14), fill=OUTLINE)
    d.point((15, 15), fill=OUTLINE)
    d.point((16, 15), fill=OUTLINE)


def draw_blush(d):
    """Pink cheeks just below the eyes."""
    d.point((9,  12), fill=BLUSH)
    d.point((22, 12), fill=BLUSH)
    d.point((9,  13), fill=BLUSH)
    d.point((22, 13), fill=BLUSH)


def draw_feet(d, left_y=0, right_y=0):
    """Two feet poking below the skirt."""
    d.rectangle([(11, 26 - left_y),  (13, 28 - left_y)],  fill=OUTLINE)
    d.rectangle([(18, 26 - right_y), (20, 28 - right_y)], fill=OUTLINE)


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
