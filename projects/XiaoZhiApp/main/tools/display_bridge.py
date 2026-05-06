#!/usr/bin/env python3
"""
display_bridge.py - PIL-based display renderer for xiaozhi.
Reads JSON commands from stdin, renders text/emoji to /dev/fb0 (RGB565).

Commands (one JSON object per line):
  {"cmd":"render","status":"LISTENING","emoji":"😊","text":"你好","code":"123456"}

Exit: EOF on stdin or {"cmd":"quit"}
"""

import sys
import os
import json
import struct
import traceback

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print(json.dumps({"event": "error", "text": "missing dependency: PIL (pillow)"}))
    sys.exit(1)

# ---- framebuffer helpers ----
FB_DEV = os.environ.get("XIAOZHI_FBDEV", os.environ.get("APPLAUNCH_LINUX_FBDEV_DEVICE", "/dev/fb0"))
WIDTH = int(os.environ.get("XIAOZHI_FB_WIDTH", "320"))
HEIGHT = int(os.environ.get("XIAOZHI_FB_HEIGHT", "170"))

# ---- font setup ----
_FONT_SEARCH = [
    os.path.join(os.path.dirname(__file__), "..", "..", "..", "assets", "NotoSansSC-Bold.ttf"),
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJKSC-Regular.otf",
    "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/noto/NotoSansSC-Regular.otf",
    "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
    "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
    "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
]

_EMOJI_FONT_SEARCH = [
    "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf",
    "/usr/share/fonts/opentype/noto/NotoColorEmoji.ttf",
    "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
]

_font_path = ""
for p in _FONT_SEARCH:
    if os.path.exists(p):
        _font_path = p
        break

if not _font_path:
    print(json.dumps({"event": "error", "text": "no TTF font found"}))
    sys.exit(1)

_emoji_font_path = _font_path
for p in _EMOJI_FONT_SEARCH:
    if os.path.exists(p):
        _emoji_font_path = p
        break

print(json.dumps({"event": "font", "text": f"text_font={_font_path} emoji_font={_emoji_font_path}"}), flush=True)

_status_font = ImageFont.truetype(_font_path, 18)
_text_font = ImageFont.truetype(_font_path, 16)
_code_font = ImageFont.truetype(_font_path, 34)
_small_font = ImageFont.truetype(_font_path, 14)


def _load_emoji_font(path, fallback_path):
    # Some emoji fonts (e.g. NotoColorEmoji) only support specific pixel sizes.
    for size in (28, 32, 24, 20, 16, 40, 48, 109, 128):
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            continue
    # Final fallback: use the normal CJK font, never fail process startup.
    return ImageFont.truetype(fallback_path, 28)


_emoji_font = _load_emoji_font(_emoji_font_path, _font_path)


def img_to_rgb565(img):
    """Convert PIL RGBA image to raw RGB565 bytes, little-endian."""
    data = bytearray()
    pixels = img.load()
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = pixels[x, y]
            if a < 128:
                r, g, b = 0, 0, 0
            rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            data.extend(struct.pack("<H", rgb565))
    return bytes(data)


# ---- state colors ----
STATE_COLORS = {
    "BINDING":   (30, 70, 140),
    "IDLE":      (40, 110, 50),
    "LISTENING": (30, 100, 170),
    "THINKING":  (180, 140, 20),
    "SPEAKING":  (160, 50, 130),
    "ERROR":     (180, 30, 30),
}

# ---- framebuffer device ----
_fb_fd = None
_fb_size = 0
_last_frame_key = None


def fb_open():
    global _fb_fd, _fb_size
    _fb_fd = os.open(FB_DEV, os.O_RDWR)
    _fb_size = WIDTH * HEIGHT * 2


def fb_write(rgb565_data):
    global _fb_fd
    if _fb_fd is not None:
        os.lseek(_fb_fd, 0, os.SEEK_SET)
        os.write(_fb_fd, rgb565_data[:_fb_size])


def render_frame(status, emoji, text, code):
    """Render a full frame and write to framebuffer."""
    global _last_frame_key
    frame_key = (status, emoji, text, code)
    if frame_key == _last_frame_key:
        return
    _last_frame_key = frame_key

    img = Image.new("RGBA", (WIDTH, HEIGHT), (220, 225, 235, 255))
    draw = ImageDraw.Draw(img, "RGBA")

    header_h = 36
    color = STATE_COLORS.get(status, STATE_COLORS["IDLE"])

    # Header bar
    draw.rectangle([0, 0, WIDTH, header_h], fill=(color[0], color[1], color[2], 255))

    # Title
    draw.text((14, 7), "XIAOZHI", font=_status_font, fill=(255, 255, 255, 255))

    # Emoji
    bbox = _emoji_font.getbbox(emoji)
    ew = bbox[2] - bbox[0]
    ex = WIDTH - ew - 14
    ey = 4
    # Add a darker bubble so color emoji never looks washed out on the header.
    draw.ellipse([ex - 6, ey - 2, ex + ew + 6, ey + 30], fill=(20, 28, 40, 220))
    draw.text((ex, ey), emoji, font=_emoji_font, fill=(255, 255, 255, 255))

    # Status label
    draw.text((14, 43), status, font=_status_font, fill=(20, 20, 40, 255))

    # Content area
    if status == "BINDING":
        draw.text((18, 67), "Open XiaoZhi app to bind", font=_small_font, fill=(80, 80, 120, 255))
        if code and len(code) == 6:
            bbox = _code_font.getbbox(code)
            cw = bbox[2] - bbox[0]
            draw.text(((WIDTH - cw) // 2, 92), code, font=_code_font, fill=(20, 20, 50, 255))
    elif status == "IDLE":
        draw.text((18, 67), "SPACE to talk", font=_small_font, fill=(80, 80, 120, 255))
    elif status == "LISTENING":
        draw.text((18, 67), "Listening...", font=_small_font, fill=(80, 80, 120, 255))
    elif status == "THINKING":
        draw.text((18, 67), "Thinking...", font=_small_font, fill=(80, 80, 120, 255))
    elif status == "SPEAKING":
        draw.text((18, 67), "Speaking...", font=_small_font, fill=(80, 80, 120, 255))
    elif status == "ERROR":
        draw.text((18, 67), "ERROR", font=_small_font, fill=(200, 40, 40, 255))

    # Bottom status bar
    bar_y = HEIGHT - 38
    draw.rectangle([0, bar_y, WIDTH, HEIGHT], fill=(50, 55, 70, 255))
    if text:
        draw.text((8, bar_y + 8), text, font=_text_font, fill=(220, 225, 240, 255))

    # Write to framebuffer
    data = img_to_rgb565(img)
    fb_write(data)

    img.close()


def main():
    global _fb_fd
    fb_open()
    print(json.dumps({"event": "connected"}), flush=True)

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            cmd = json.loads(line)
        except json.JSONDecodeError:
            print(f"[display-bridge] parse error: {line[:80]}", file=sys.stderr, flush=True)
            continue

        if cmd.get("cmd") == "quit":
            break

        if cmd.get("cmd") == "render":
            try:
                status = cmd.get("status", "IDLE")
                emoji = cmd.get("emoji", "😄")
                text = cmd.get("text", "")
                code = cmd.get("code", "")
                print(f"[display-bridge] render status={status} emoji={emoji} text={text[:30]}", file=sys.stderr, flush=True)
                render_frame(status, emoji, text, code)
            except Exception:
                traceback.print_exc(file=sys.stderr)

    if _fb_fd is not None:
        os.close(_fb_fd)
        _fb_fd = None


if __name__ == "__main__":
    main()
