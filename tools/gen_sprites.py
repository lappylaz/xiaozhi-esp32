#!/usr/bin/env python3
"""Generate Sabrina sprites — Chibilunatchi-inspired Tamagotchi character.

Output format matches xiaozhi's existing emoji assets:
  * 64x64 RGB565A8 (RGB565 plane + 8-bit alpha plane = 12 288 bytes per
    sprite).
  * lv_image_dsc_t static const declarations (one per frame).
  * Header file: main/assets/sprites/sabrina_sprites.h

Reference: https://tamagotchifanon.fandom.com/wiki/Chibilunatchi
  - pink head + cat ears (top) + lopped rabbit ears (sides)
  - red eyes, pink forehead tuft, heart bow with ribbon dangles
  - blue dress, red stockings, dark shoes

Frames generated:
  - sabrina_idle_a, sabrina_idle_b   (subtle 2-frame breathing/idle bob)
  - sabrina_blink                    (eyes shut briefly)
  - sabrina_walk_a, sabrina_walk_b   (2-frame walk cycle)
  - sabrina_happy_a, sabrina_happy_b (smiling, slight hop)
  - sabrina_sleep_a, sabrina_sleep_b (snoozing, Z fades in/out)
  - sabrina_talk_a, sabrina_talk_b   (mouth open/closed for speaking)

Run: python3 tools/gen_sprites.py
"""

from PIL import Image, ImageDraw
import os

W, H = 64, 64
SCALE = 4  # preview scale factor (4x = 256x256 PNG)

# -------- Palette ----------------------------------------------------------
BG       = (0,   0,   0)        # transparent
BODY     = (255, 175, 210)      # head + ears (light pink)
BODY_S   = (255, 200, 225)      # body highlight
BODY_D   = (210, 130, 170)      # body shading
OUTLINE  = (90,  40,  80)       # dark purple-brown outline
WHITE    = (255, 255, 255)
BLACK    = (10,  10,  20)
BLUSH    = (255, 110, 160)
HAIR     = (255, 100, 150)      # forehead tuft + bow base
HAIR_D   = (200, 60,  110)      # darker pink accents
EYE_R    = (220, 40,  70)       # red eyes
EYE_R_D  = (140, 20,  40)       # red eye outline
DRESS    = (110, 160, 220)      # blue dress
DRESS_S  = (160, 200, 240)      # dress highlight
DRESS_D  = (60,  100, 170)      # dress fold / hem
STOCK    = (210, 50,  60)       # red stockings
STOCK_D  = (140, 30,  40)       # stocking shading
HEART    = (240, 60,  100)      # bow heart
ZZZ      = (160, 200, 255)      # sleep Z


# -------- Drawing helpers --------------------------------------------------

def new_canvas():
    return Image.new("RGB", (W, H), BG)


def draw_cat_ears(d, dy=0):
    """Two big pink triangular cat ears flanking the bow."""
    # Left ear (bigger, taller, more visible)
    d.polygon([(14, 16 + dy), (26, 16 + dy), (18,  0 + dy)], fill=BODY, outline=OUTLINE)
    # Inner ear pink
    d.polygon([(17, 14 + dy), (24, 14 + dy), (20,  4 + dy)], fill=HAIR, outline=None)
    # Inner ear darker accent
    d.line([(20, 6 + dy), (20, 12 + dy)], fill=HAIR_D)
    # Right ear (mirror)
    d.polygon([(38, 16 + dy), (50, 16 + dy), (46,  0 + dy)], fill=BODY, outline=OUTLINE)
    d.polygon([(40, 14 + dy), (47, 14 + dy), (44,  4 + dy)], fill=HAIR, outline=None)
    d.line([(44, 6 + dy), (44, 12 + dy)], fill=HAIR_D)


def draw_rabbit_ears(d, dy=0):
    """Lopped rabbit ears — start at upper head sides, curl up and outward,
    then fold/flop at the tip. Drawn as 3 overlapping ellipses each."""
    # ----- Left ear -----
    # Upper segment (vertical oblong, attached to head)
    d.ellipse([6,  14 + dy, 16, 28 + dy], fill=BODY, outline=OUTLINE)
    # Mid bend (slightly outward)
    d.ellipse([2,  22 + dy, 14, 36 + dy], fill=BODY, outline=OUTLINE)
    # Floppy tip (lopped — falls outward and downward)
    d.ellipse([0,  30 + dy, 12, 44 + dy], fill=BODY, outline=OUTLINE)
    # Inner ear pink stripe
    d.line([(9, 18 + dy), (8, 26 + dy), (6, 33 + dy), (5, 39 + dy)], fill=HAIR)
    d.line([(8, 26 + dy), (6, 33 + dy)], fill=HAIR_D)

    # ----- Right ear (mirror) -----
    d.ellipse([48, 14 + dy, 58, 28 + dy], fill=BODY, outline=OUTLINE)
    d.ellipse([50, 22 + dy, 62, 36 + dy], fill=BODY, outline=OUTLINE)
    d.ellipse([52, 30 + dy, 64, 44 + dy], fill=BODY, outline=OUTLINE)
    d.line([(55, 18 + dy), (56, 26 + dy), (58, 33 + dy), (59, 39 + dy)], fill=HAIR)
    d.line([(56, 26 + dy), (58, 33 + dy)], fill=HAIR_D)


def draw_bow(d, dy=0):
    """Heart-centered bow on top of head, between the cat ears."""
    by = 4 + dy
    # Bow loops
    d.ellipse([20, by,     30, by + 12], fill=HAIR,   outline=OUTLINE)
    d.ellipse([34, by,     44, by + 12], fill=HAIR,   outline=OUTLINE)
    # Center band
    d.rectangle([(29, by + 2), (35, by + 10)], fill=HAIR_D, outline=OUTLINE)
    # Heart in center band (proper heart shape)
    d.point((30, by + 4), fill=HEART)
    d.point((31, by + 4), fill=HEART)
    d.point((33, by + 4), fill=HEART)
    d.point((34, by + 4), fill=HEART)
    d.line([(30, by + 5), (34, by + 5)], fill=HEART)
    d.line([(31, by + 6), (33, by + 6)], fill=HEART)
    d.point((32, by + 7), fill=HEART)
    # Sparkle highlight
    d.point((30, by + 4), fill=WHITE)
    # Two ribbon dangles below the bow
    d.line([(26, by + 12), (24, by + 18)], fill=HAIR,   width=2)
    d.line([(38, by + 12), (40, by + 18)], fill=HAIR,   width=2)
    d.point((23, by + 18), fill=HAIR_D)
    d.point((41, by + 18), fill=HAIR_D)


def draw_head(d, dy=0):
    """Pink head circle. Slight shading on lower right."""
    d.ellipse([16, 14 + dy, 48, 42 + dy], fill=BODY,   outline=OUTLINE)
    # Soft shading on lower-right curve
    d.ellipse([34, 30 + dy, 46, 40 + dy], fill=BODY_D, outline=None)
    d.ellipse([16, 14 + dy, 48, 42 + dy], fill=None,   outline=OUTLINE)
    # Highlight on upper-left (gives roundness)
    d.point((22, 19 + dy), fill=BODY_S)
    d.point((23, 19 + dy), fill=BODY_S)
    d.point((22, 20 + dy), fill=BODY_S)


def draw_forehead_tuft(d, dy=0):
    """Pink hair tuft on the forehead — three little points (signature
    feature inherited from her father)."""
    # Three downward-pointing hair points
    d.polygon([(24, 16 + dy), (28, 16 + dy), (26, 22 + dy)], fill=HAIR,   outline=OUTLINE)
    d.polygon([(28, 16 + dy), (32, 16 + dy), (30, 24 + dy)], fill=HAIR_D, outline=OUTLINE)
    d.polygon([(32, 16 + dy), (36, 16 + dy), (34, 22 + dy)], fill=HAIR,   outline=OUTLINE)


def draw_eyes_open(d, dy=0):
    """Big anime-style red eyes."""
    # Left eye
    d.ellipse([20, 24 + dy, 28, 32 + dy], fill=WHITE, outline=BLACK)
    d.ellipse([22, 25 + dy, 27, 31 + dy], fill=EYE_R, outline=EYE_R_D)
    d.ellipse([23, 26 + dy, 25, 29 + dy], fill=BLACK, outline=None)
    # Highlight sparkles
    d.point((24, 26 + dy), fill=WHITE)
    d.point((25, 30 + dy), fill=WHITE)
    # Right eye
    d.ellipse([36, 24 + dy, 44, 32 + dy], fill=WHITE, outline=BLACK)
    d.ellipse([38, 25 + dy, 43, 31 + dy], fill=EYE_R, outline=EYE_R_D)
    d.ellipse([39, 26 + dy, 41, 29 + dy], fill=BLACK, outline=None)
    d.point((40, 26 + dy), fill=WHITE)
    d.point((41, 30 + dy), fill=WHITE)
    # Eyelashes (outer corners)
    d.line([(20, 24 + dy), (18, 22 + dy)], fill=BLACK)
    d.line([(20, 25 + dy), (18, 24 + dy)], fill=BLACK)
    d.line([(44, 24 + dy), (46, 22 + dy)], fill=BLACK)
    d.line([(44, 25 + dy), (46, 24 + dy)], fill=BLACK)


def draw_eyes_blink(d, dy=0):
    """Eyes closed flat — blinking."""
    d.line([(20, 28 + dy), (28, 28 + dy)], fill=BLACK, width=2)
    d.line([(36, 28 + dy), (44, 28 + dy)], fill=BLACK, width=2)
    # Lashes
    d.line([(20, 28 + dy), (18, 26 + dy)], fill=BLACK)
    d.line([(44, 28 + dy), (46, 26 + dy)], fill=BLACK)


def draw_eyes_squint(d, dy=0):
    """Eyes ^ ^ — happy squint."""
    d.line([(20, 30 + dy), (24, 24 + dy)], fill=BLACK, width=2)
    d.line([(24, 24 + dy), (28, 30 + dy)], fill=BLACK, width=2)
    d.line([(36, 30 + dy), (40, 24 + dy)], fill=BLACK, width=2)
    d.line([(40, 24 + dy), (44, 30 + dy)], fill=BLACK, width=2)


def draw_eyes_sleep(d, dy=0):
    """Eyes downward arc — sleeping."""
    for x_off in (20, 36):
        d.line([(x_off,     28 + dy),
                (x_off + 2, 30 + dy),
                (x_off + 4, 30 + dy),
                (x_off + 6, 30 + dy),
                (x_off + 8, 28 + dy)], fill=BLACK)


def draw_blush(d, dy=0):
    """Pink cheek dots."""
    d.ellipse([18, 32 + dy, 21, 35 + dy], fill=BLUSH, outline=None)
    d.ellipse([43, 32 + dy, 46, 35 + dy], fill=BLUSH, outline=None)


def draw_blush_big(d, dy=0):
    """Bigger blush for happy."""
    d.ellipse([17, 31 + dy, 22, 36 + dy], fill=BLUSH, outline=None)
    d.ellipse([42, 31 + dy, 47, 36 + dy], fill=BLUSH, outline=None)


def draw_mouth_smile(d, dy=0):
    d.line([(28, 36 + dy), (30, 38 + dy), (34, 38 + dy), (36, 36 + dy)], fill=OUTLINE)


def draw_mouth_open(d, dy=0):
    """Open smile (talking / happy)."""
    d.ellipse([28, 36 + dy, 36, 40 + dy], fill=HAIR_D, outline=OUTLINE)
    d.line([(29, 37 + dy), (35, 37 + dy)], fill=WHITE)  # tooth highlight


def draw_mouth_yawn(d, dy=0):
    d.ellipse([30, 36 + dy, 34, 40 + dy], fill=HAIR_D, outline=OUTLINE)


def draw_dress(d):
    """Blue dress flaring out below the head (no dy — feet stay grounded)."""
    # Main body of the dress (flared trapezoid)
    poly = [(20, 40), (44, 40), (54, 58), (10, 58)]
    d.polygon(poly, fill=DRESS, outline=OUTLINE)
    # Vertical fold highlights
    d.line([(26, 42), (18, 58)], fill=DRESS_S)
    d.line([(32, 42), (32, 58)], fill=DRESS_S)
    d.line([(38, 42), (46, 58)], fill=DRESS_S)
    # Inner shading near hem
    d.line([(12, 56), (52, 56)], fill=DRESS_D)
    # Collar accent — small pink ribbon at neckline
    d.ellipse([28, 39, 30, 42], fill=HAIR,  outline=OUTLINE)
    d.ellipse([34, 39, 36, 42], fill=HAIR,  outline=OUTLINE)
    d.point((32, 41), fill=HEART)


def draw_legs(d, left_y=0, right_y=0):
    """Red stockings + small dark shoes peeking below the dress.
    left_y / right_y shift each leg upward for the walk cycle."""
    # Left leg
    ly = 56 - left_y
    d.rectangle([(22, ly),     (26, ly + 4)], fill=STOCK,   outline=STOCK_D)
    d.rectangle([(21, ly + 4), (27, ly + 5)], fill=OUTLINE)  # shoe
    # Right leg
    ry = 56 - right_y
    d.rectangle([(38, ry),     (42, ry + 4)], fill=STOCK,   outline=STOCK_D)
    d.rectangle([(37, ry + 4), (43, ry + 5)], fill=OUTLINE)


def draw_z(d, fade=False):
    """Sleep 'Z' floating above. If fade=True, draw with lighter color
    (gives a 2-frame fade-in/fade-out animation when paired with non-fade)."""
    color = (200, 220, 255) if fade else ZZZ
    # Big Z in upper-right (clear of bow)
    d.line([(50, 8),  (58, 8)],  fill=color, width=2)
    d.line([(58, 8),  (50, 16)], fill=color, width=2)
    d.line([(50, 16), (58, 16)], fill=color, width=2)
    if not fade:
        # Smaller z slightly above-left when not faded
        d.line([(46, 4), (52, 4)], fill=color)
        d.line([(52, 4), (46, 7)], fill=color)
        d.line([(46, 7), (52, 7)], fill=color)


# -------- Assemble each frame ---------------------------------------------

def draw_full_character(d, *, eye_kind="open", mouth_kind="smile",
                        head_dy=0, leg_l=0, leg_r=0,
                        blush_kind="normal", show_z=False, z_fade=False,
                        forehead_tuft=True, dress=True):
    """Layered draw: ears -> bow -> dress -> head -> face -> legs -> z.
    No more side rabbit ears — they read as pigtails at this resolution.
    Just the (now-bigger) cat ears + heart bow on top."""
    # Cat ears + bow on top of head
    draw_cat_ears(d, dy=head_dy)
    draw_bow(d,      dy=head_dy)
    # Dress (drawn before head so head can overlap dress collar)
    if dress:
        draw_dress(d)
    # Head + face
    draw_head(d, dy=head_dy)
    if forehead_tuft:
        draw_forehead_tuft(d, dy=head_dy)
    if eye_kind == "open":
        draw_eyes_open(d, dy=head_dy)
    elif eye_kind == "blink":
        draw_eyes_blink(d, dy=head_dy)
    elif eye_kind == "squint":
        draw_eyes_squint(d, dy=head_dy)
    elif eye_kind == "sleep":
        draw_eyes_sleep(d, dy=head_dy)
    if blush_kind == "big":
        draw_blush_big(d, dy=head_dy)
    else:
        draw_blush(d, dy=head_dy)
    if mouth_kind == "smile":
        draw_mouth_smile(d, dy=head_dy)
    elif mouth_kind == "open":
        draw_mouth_open(d, dy=head_dy)
    elif mouth_kind == "yawn":
        draw_mouth_yawn(d, dy=head_dy)
    # Legs (always grounded — don't shift with head_dy so the bob looks like
    # a head/torso movement on top of stationary feet).
    draw_legs(d, left_y=leg_l, right_y=leg_r)
    # Sleep Z floats over everything
    if show_z:
        draw_z(d, fade=z_fade)


# -------- Frame builders ---------------------------------------------------

def make(eye="open", mouth="smile", head_dy=0, leg_l=0, leg_r=0,
         blush="normal", show_z=False, z_fade=False):
    img = new_canvas()
    d = ImageDraw.Draw(img)
    draw_full_character(d, eye_kind=eye, mouth_kind=mouth,
                        head_dy=head_dy, leg_l=leg_l, leg_r=leg_r,
                        blush_kind=blush, show_z=show_z, z_fade=z_fade)
    return img


SPRITES = {
    # Idle — subtle 2-frame bob (head down 0px / down 1px) + occasional blink
    "sabrina_idle_a":   lambda: make(),
    "sabrina_idle_b":   lambda: make(head_dy=1),
    "sabrina_blink":    lambda: make(eye="blink"),
    # Walk — alternating leg lift, slight head bob synchronized
    "sabrina_walk_a":   lambda: make(leg_l=2, leg_r=0, head_dy=0),
    "sabrina_walk_b":   lambda: make(leg_l=0, leg_r=2, head_dy=1),
    # Happy — squint eyes + open smile, second frame is a tiny hop
    "sabrina_happy_a":  lambda: make(eye="squint", mouth="open", blush="big"),
    "sabrina_happy_b":  lambda: make(eye="squint", mouth="open", blush="big",
                                     head_dy=-1, leg_l=1, leg_r=1),
    # Sleep — closed eyes + Z; z_fade=True for second frame
    "sabrina_sleep_a":  lambda: make(eye="sleep", mouth="yawn", show_z=True),
    "sabrina_sleep_b":  lambda: make(eye="sleep", mouth="yawn", show_z=True, z_fade=True),
    # Talk — mouth open / closed cycle (for assistant speaking states)
    "sabrina_talk_a":   lambda: make(mouth="open"),
    "sabrina_talk_b":   lambda: make(mouth="smile"),
}


# -------- RGB565A8 conversion ---------------------------------------------

def rgb_to_rgb565(rgb):
    r, g, b = rgb
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def img_to_rgb565a8_bytes(img):
    pixels = list(img.getdata())
    rgb = bytearray()
    alpha = bytearray()
    for r, g, b in pixels:
        v = rgb_to_rgb565((r, g, b))
        rgb.append(v & 0xFF)
        rgb.append((v >> 8) & 0xFF)
        alpha.append(0x00 if (r, g, b) == BG else 0xFF)
    return bytes(rgb) + bytes(alpha)


def emit_c_array(name, data):
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


# -------- Main ------------------------------------------------------------

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
        "// Sabrina sprites — Chibilunatchi-inspired Tamagotchi character.",
        f"// Each sprite is {W}x{H} RGB565A8 ({W*H*2} RGB + {W*H} alpha = {W*H*3} bytes).",
        "#pragma once",
        "",
        '#include "lvgl.h"',
        "",
    ]

    for name, fn in SPRITES.items():
        img = fn()
        scaled = img.resize((W * SCALE, H * SCALE), Image.NEAREST)
        scaled.save(os.path.join(preview_dir, f"{name}.png"))
        data = img_to_rgb565a8_bytes(img)
        assert len(data) == W * H * 3, f"bad data length: {len(data)}"
        header_lines.append(emit_c_array(name, data))
        header_lines.append("")

    with open(out_path, "w") as f:
        f.write("\n".join(header_lines))

    total = len(SPRITES) * W * H * 3
    print(f"Generated {len(SPRITES)} sprites:")
    print(f"  Header:   {out_path}")
    print(f"  Previews: {preview_dir}/*.png")
    print(f"  Per-sprite size: {W * H * 3} bytes")
    print(f"  Total embedded: {total} bytes ({total // 1024} KB)")


if __name__ == "__main__":
    main()
