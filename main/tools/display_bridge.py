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
import select
import time

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
    "/usr/share/fonts/truetype/xiaozhi/NotoSansSC-Regular.ttf",
    "/usr/share/fonts/truetype/xiaozhi/NotoSansSC-Bold.ttf",
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
    "/usr/share/fonts/truetype/xiaozhi/NotoColorEmoji.ttf",
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
if os.environ.get("XIAOZHI_USE_COLOR_EMOJI", "1") == "1":
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
    for size in (24, 22, 20, 18, 16, 28, 32, 40, 48, 64, 96, 109, 128):
        try:
            return ImageFont.truetype(path, size), size
        except OSError:
            continue
    # Final fallback: use the normal CJK font, never fail process startup.
    return ImageFont.truetype(fallback_path, 22), 22


_emoji_font, _emoji_font_px = _load_emoji_font(_emoji_font_path, _font_path)
_emoji_use_embedded = "NotoColorEmoji" in _emoji_font_path
print(
    json.dumps(
        {
            "event": "emoji",
            "text": f"embedded={int(_emoji_use_embedded)} px={_emoji_font_px} path={_emoji_font_path}",
        }
    ),
    flush=True,
)


def _normalize_emoji(s):
    if not s:
        return "😄"
    # Strip text/emoji variation selectors; they can cause fallback glyph artifacts on some stacks.
    return s.replace("\ufe0f", "").replace("\ufe0e", "")


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

# ---- line-wrap state ----
_line_wrap_lines = []
_line_wrap_line = 0
_line_wrap_next_ts = 0.0
_line_wrap_current_text = ""
_LINE_SHOW_S = 2.0
_LINE_EXTEND_S = 1.0


def fb_open():
    global _fb_fd, _fb_size
    _fb_fd = os.open(FB_DEV, os.O_RDWR)
    _fb_size = WIDTH * HEIGHT * 2


def fb_write(rgb565_data):
    global _fb_fd
    if _fb_fd is not None:
        os.lseek(_fb_fd, 0, os.SEEK_SET)
        data = rgb565_data[:_fb_size]
        total = 0
        while total < len(data):
            n = os.write(_fb_fd, data[total:])
            if n <= 0:
                break
            total += n


def _wrap_text(text, font, max_width):
    """Wrap text into lines that each fit within max_width using the given font."""
    if not text:
        return [""]
    lines = []
    current = ""
    for ch in text:
        test = current + ch
        bbox = font.getbbox(test)
        w = bbox[2] - bbox[0]
        if w <= max_width:
            current = test
        else:
            if current:
                lines.append(current)
            current = ch
    if current:
        lines.append(current)
    return lines or [text]


def render_frame(status, emoji, text, code):
    """Render a full frame and write to framebuffer."""
    global _last_frame_key
    global _line_wrap_lines, _line_wrap_line, _line_wrap_next_ts, _line_wrap_current_text
    img = Image.new("RGBA", (WIDTH, HEIGHT), (220, 225, 235, 255))
    draw = ImageDraw.Draw(img, "RGBA")

    header_h = 36
    color = STATE_COLORS.get(status, STATE_COLORS["IDLE"])

    # Header bar
    draw.rectangle([0, 0, WIDTH, header_h], fill=(color[0], color[1], color[2], 255))

    # Title
    draw.text((8, 7), "XIAOZHI", font=_status_font, fill=(255, 255, 255, 255))

    # Emoji
    emoji = _normalize_emoji(emoji)
    emoji_target = 29
    if _emoji_use_embedded:
        # Render color glyph then resize with alpha-safe nearest sampling to avoid dark fringes.
        scratch = Image.new("RGBA", (160, 160), (0, 0, 0, 0))
        sdraw = ImageDraw.Draw(scratch, "RGBA")
        sdraw.text((0, 0), emoji, font=_emoji_font, embedded_color=True)
        eb = scratch.getbbox()
        if eb is not None:
            eg = scratch.crop(eb)
            ratio = eg.height / max(1, eg.width)
            tw = max(emoji_target, int(emoji_target / max(0.6, ratio)))
            th = max(emoji_target, int(emoji_target * max(0.6, ratio)))
            eg = eg.resize((tw, th), Image.Resampling.NEAREST)
            ex = WIDTH - eg.width - 8
            ey = header_h + 6
            img.alpha_composite(eg, (ex, ey))
        else:
            draw.text((WIDTH - 30, header_h + 6), emoji, font=_text_font, fill=(255, 255, 255, 255))
    else:
        bbox = _emoji_font.getbbox(emoji)
        ew = bbox[2] - bbox[0]
        ex = WIDTH - ew - 8
        ey = header_h + 6
        draw.text((ex, ey), emoji, font=_emoji_font, fill=(255, 255, 255, 255))

    # Status label
    draw.text((8, 43), status, font=_status_font, fill=(20, 20, 40, 255))

    # Content area
    if status == "BINDING":
        draw.text((8, 67), "Open XiaoZhi app to bind", font=_small_font, fill=(80, 80, 120, 255))
        if code and len(code) == 6:
            bbox = _code_font.getbbox(code)
            cw = bbox[2] - bbox[0]
            draw.text(((WIDTH - cw) // 2, 92), code, font=_code_font, fill=(20, 20, 50, 255))
    elif status == "IDLE":
        draw.text((8, 67), "SPACE to talk", font=_small_font, fill=(80, 80, 120, 255))
    elif status == "LISTENING":
        draw.text((8, 67), "Listening...", font=_small_font, fill=(80, 80, 120, 255))
    elif status == "THINKING":
        draw.text((8, 67), "Thinking...", font=_small_font, fill=(80, 80, 120, 255))
    elif status == "SPEAKING":
        draw.text((8, 67), "Speaking...", font=_small_font, fill=(80, 80, 120, 255))
    elif status == "ERROR":
        draw.text((8, 67), "ERROR", font=_small_font, fill=(200, 40, 40, 255))

    # Bottom status bar - line-wrap display
    bar_y = HEIGHT - 38
    draw.rectangle([0, bar_y, WIDTH, HEIGHT], fill=(50, 55, 70, 255))
    if text:
        x0 = 8
        y0 = bar_y + 8
        avail_w = WIDTH - 16
        tb = _text_font.getbbox(text)
        text_w = max(0, tb[2] - tb[0])
        if text_w <= avail_w:
            draw.text((x0, y0), text, font=_text_font, fill=(220, 225, 240, 255))
            _line_wrap_lines = []
            _line_wrap_current_text = ""
            _line_wrap_line = 0
            line_bucket = 0
        else:
            if text != _line_wrap_current_text:
                prev_line = _line_wrap_line
                _line_wrap_lines = _wrap_text(text, _text_font, avail_w)
                _line_wrap_current_text = text
                if _line_wrap_lines and prev_line < len(_line_wrap_lines):
                    _line_wrap_line = prev_line
                else:
                    _line_wrap_line = 0
                _line_wrap_next_ts = time.monotonic() + _LINE_SHOW_S + _LINE_EXTEND_S
            else:
                now = time.monotonic()
                if now >= _line_wrap_next_ts and _line_wrap_line + 1 < len(_line_wrap_lines):
                    _line_wrap_line += 1
                    _line_wrap_next_ts = now + _LINE_SHOW_S
            idx = min(_line_wrap_line, max(0, len(_line_wrap_lines) - 1))
            if _line_wrap_lines and idx < len(_line_wrap_lines):
                draw.text((x0, y0), _line_wrap_lines[idx], font=_text_font, fill=(220, 225, 240, 255))
            line_bucket = _line_wrap_line
    else:
        _line_wrap_lines = []
        _line_wrap_current_text = ""
        _line_wrap_line = 0
        line_bucket = 0

    frame_key = (status, emoji, text, code, line_bucket)
    global _last_frame_key
    if frame_key == _last_frame_key:
        img.close()
        return
    _last_frame_key = frame_key

    # Write to framebuffer
    data = img_to_rgb565(img)
    fb_write(data)

    img.close()


def main():
    global _fb_fd
    fb_open()
    print(json.dumps({"event": "connected"}), flush=True)

    current = {
        "status": "IDLE",
        "emoji": "😄",
        "text": "",
        "code": "",
        "has_frame": False,
    }

    while True:
        readable, _, _ = select.select([sys.stdin], [], [], 0.08)
        if readable:
            line = sys.stdin.readline()
            if not line:
                break
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
                    current["status"] = cmd.get("status", "IDLE")
                    current["emoji"] = cmd.get("emoji", "😄")
                    current["text"] = cmd.get("text", "")
                    current["code"] = cmd.get("code", "")
                    current["has_frame"] = True
                except Exception:
                    # write to stdout (NOT stderr) to allow C++ drain thread to consume it
                    traceback.print_exc()

        if current["has_frame"]:
            try:
                render_frame(current["status"], current["emoji"], current["text"], current["code"])
            except Exception:
                traceback.print_exc(file=sys.stderr)

    if _fb_fd is not None:
        os.close(_fb_fd)
        _fb_fd = None


if __name__ == "__main__":
    main()
