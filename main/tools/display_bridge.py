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
import array
import base64
import importlib.util
import math
from io import BytesIO

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print(json.dumps({"event": "error", "text": "missing dependency: PIL (pillow)"}))
    sys.exit(1)

try:
    import numpy as np
except ImportError:
    np = None

# CairoSVG has a comparatively expensive import on small Raspberry Pi models.
# Load it only when an SVG emoji is actually needed so the display handshake is
# not held up by an optional renderer.
_cairosvg = None
_cairosvg_checked = False

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))

# ---- framebuffer helpers ----
FB_DEV = os.environ.get("XIAOZHI_FBDEV", os.environ.get("APPLAUNCH_LINUX_FBDEV_DEVICE", "/dev/fb0"))
WIDTH = int(os.environ.get("XIAOZHI_FB_WIDTH", "320"))
HEIGHT = int(os.environ.get("XIAOZHI_FB_HEIGHT", "170"))
SNAPSHOT_PATH = os.environ.get("XIAOZHI_RENDER_PNG_PATH", "")
UI_STYLE = os.environ.get(
    "XIAOZHI_DISPLAY_UI_STYLE", os.environ.get("DISPLAY_UI_STYLE", "classic")
).strip().lower()
if UI_STYLE not in {"classic", "watercolor"}:
    UI_STYLE = "classic"
WATERCOLOR_FPS = max(1, min(20, int(os.environ.get("XIAOZHI_WATERCOLOR_FPS", "15"))))
WATERCOLOR_DIAMETER = max(
    80,
    min(HEIGHT - 20, int(os.environ.get("XIAOZHI_WATERCOLOR_DIAMETER", str(round(HEIGHT * 2 / 3))))),
)
WATERCOLOR_RENDER_SCALE = max(
    0.2, min(1.0, float(os.environ.get("XIAOZHI_WATERCOLOR_RENDER_SCALE", "0.60")))
)
WATERCOLOR_SMOOTH_FBM = os.environ.get("XIAOZHI_WATERCOLOR_SMOOTH_FBM", "true").strip().lower() in {
    "1", "true", "yes", "on"
}
WATERCOLOR_TEMPORAL_3D = os.environ.get("XIAOZHI_WATERCOLOR_TEMPORAL_3D", "true").strip().lower() in {
    "1", "true", "yes", "on"
}
WATERCOLOR_THREADS = max(1, min(4, int(os.environ.get("XIAOZHI_WATERCOLOR_THREADS", "2"))))
WATERCOLOR_PANE_WIDTH = min(WIDTH, WATERCOLOR_DIAMETER + 8)
WATERCOLOR_PANE_HEIGHT = max(WATERCOLOR_DIAMETER + 20, HEIGHT - 12)

# ---- font setup ----
_FONT_SEARCH = [
    os.path.join(REPO_ROOT, "tools", "fonts", "NotoSansSC-Regular.ttf"),
    os.path.join(REPO_ROOT, "tools", "fonts", "NotoSansSC-Bold.ttf"),
    "/usr/share/APPLaunch/share/xiaozhi/fonts/NotoSansSC-Regular.ttf",
    "/usr/share/APPLaunch/share/xiaozhi/fonts/NotoSansSC-Bold.ttf",
    "/usr/share/fonts/truetype/xiaozhi/NotoSansSC-Regular.ttf",
    "/usr/share/fonts/truetype/xiaozhi/NotoSansSC-Bold.ttf",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJKSC-Regular.otf",
    "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/noto/NotoSansSC-Regular.otf",
    "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
    "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
    "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
    "/System/Library/Fonts/PingFang.ttc",
    "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
    os.path.join(REPO_ROOT, "tools", "fonts", "NotoSansCJKsc-Regular.otf"),
]

_EMOJI_FONT_SEARCH = [
    os.path.join(REPO_ROOT, "tools", "fonts", "NotoColorEmoji.ttf"),
    "/usr/share/APPLaunch/share/xiaozhi/fonts/NotoColorEmoji.ttf",
    "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf",
    "/usr/share/fonts/opentype/noto/NotoColorEmoji.ttf",
    "/usr/share/fonts/truetype/xiaozhi/NotoColorEmoji.ttf",
    "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
    "/System/Library/Fonts/Apple Color Emoji.ttc",
]

_EMOJI_SVG_SEARCH = [
    os.path.join(REPO_ROOT, "tools", "emoji_svg"),
    "/usr/share/APPLaunch/share/xiaozhi/emoji_svg",
]
_emoji_svg_dir = next((p for p in _EMOJI_SVG_SEARCH if os.path.isdir(p)), "")

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
_terminal_font = ImageFont.truetype(_font_path, 9)


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
_emoji_use_embedded = any(
    marker in _emoji_font_path for marker in ("NotoColorEmoji", "Apple Color Emoji")
)
print(
    json.dumps(
        {
            "event": "emoji",
            "text": f"embedded={int(_emoji_use_embedded)} px={_emoji_font_px} path={_emoji_font_path}",
        }
    ),
    flush=True,
)


def _load_watercolor_renderer(force=False):
    if not force and UI_STYLE != "watercolor":
        return None
    extension = os.path.join(SCRIPT_DIR, "_watercolor_rust.so")
    if not os.path.isfile(extension):
        raise RuntimeError(
            "watercolor mode requires _watercolor_rust.so; rerun tools/install.sh"
        )
    spec = importlib.util.spec_from_file_location("_watercolor_rust", extension)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"unable to load watercolor renderer: {extension}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.OrbRenderer(
        WATERCOLOR_PANE_WIDTH,
        WATERCOLOR_PANE_HEIGHT,
        WATERCOLOR_DIAMETER,
        WATERCOLOR_RENDER_SCALE,
        WATERCOLOR_SMOOTH_FBM,
        WATERCOLOR_TEMPORAL_3D,
        3.6,
        0.70,
        4.5,
        WATERCOLOR_THREADS,
    )


try:
    _watercolor = _load_watercolor_renderer()
except Exception as exc:
    print(json.dumps({"event": "error", "text": str(exc)}), flush=True)
    sys.exit(1)

_watercolor_phase = 0.0
_watercolor_clock = time.monotonic()
_watercolor_last_frame = 0.0
_audio_features = {
    "user": {
        "level": 0.0,
        "peak": 0.0,
        "bands": (0.0, 0.0, 0.0, 0.0),
        "cumulative": [0.0, 0.0, 0.0, 0.0],
        "updated_at": 0.0,
    },
    "assistant": {
        "level": 0.0,
        "peak": 0.0,
        "bands": (0.0, 0.0, 0.0, 0.0),
        "cumulative": [0.0, 0.0, 0.0, 0.0],
        "updated_at": 0.0,
    },
}


def _update_audio_features(pcm_b64, sample_rate, role):
    """Match whisplay-xiaozhi's PCM-driven watercolor analysis."""
    if _watercolor is None or np is None or role not in _audio_features or not pcm_b64:
        return
    try:
        pcm = base64.b64decode(pcm_b64, validate=True)
        samples = np.frombuffer(pcm, dtype="<i2").astype(np.float32) / 32768.0
    except (ValueError, TypeError):
        return
    if samples.size < 16:
        return

    samples -= float(np.mean(samples))
    rms = float(np.sqrt(np.mean(samples * samples)))
    peak = float(np.max(np.abs(samples)))
    level = min(1.0, rms * 5.5)
    window = np.hanning(samples.size)
    spectrum_amplitude = (
        np.abs(np.fft.rfft(samples * window))
        * (2.0 / max(1.0, float(np.sum(window))))
    )
    spectrum_db = 20.0 * np.log10(np.maximum(spectrum_amplitude, 1.0e-8))
    spectrum = np.sqrt(
        np.clip(1.0 + np.clip(spectrum_db, -100.0, -10.0) / 100.0, 0.0, 1.0)
    )
    sample_rate = max(1, int(sample_rate))
    frequencies = np.fft.rfftfreq(samples.size, 1.0 / sample_rate)
    nyquist = max(40.0, sample_rate * 0.5)
    edges = np.geomspace(20.0, nyquist, 4)
    raw_bands = []
    for index, (low, high) in enumerate(zip(edges[:-1], edges[1:])):
        values = spectrum[(frequencies >= low) & (frequencies < high)]
        magnitude = float(np.median(values)) if values.size else 0.0
        magnitude *= (10.0, 1.0, 1.0)[index]
        raw_bands.append(magnitude / (magnitude + 1.0))
    audible = spectrum[(frequencies >= 20.0) & (frequencies < nyquist)]
    overall = float(np.median(audible)) if audible.size else 0.0
    overall = overall / (overall + 1.0)
    raw_audio = (*raw_bands, overall)
    duration = samples.size / sample_rate
    smoothing = 1.0 - math.exp(-duration / 2.0)
    target = _audio_features[role]
    target["level"] = level
    target["peak"] = min(1.0, peak * 1.8)
    target["bands"] = tuple(
        previous + (value - previous) * smoothing
        for previous, value in zip(target["bands"], raw_audio)
    )
    for index, value in enumerate(raw_audio):
        target["cumulative"][index] += value * duration * 20.0
    target["updated_at"] = time.monotonic()


def _normalize_emoji(s):
    if not s:
        return "😄"
    # Strip text/emoji variation selectors; they can cause fallback glyph artifacts on some stacks.
    return s.replace("\ufe0f", "").replace("\ufe0e", "")


_emoji_svg_cache = {}


def _get_cairosvg():
    global _cairosvg, _cairosvg_checked
    if not _cairosvg_checked:
        _cairosvg_checked = True
        try:
            import cairosvg as module
            _cairosvg = module
        except ImportError:
            _cairosvg = None
    return _cairosvg


def _emoji_svg_image(emoji, size):
    """Render the same SVG emoji artwork used by whisplay-xiaozhi."""
    if not emoji or not _emoji_svg_dir:
        return None
    normalized = emoji.replace("\ufe0e", "").replace("\ufe0f", "")
    candidates = [emoji, normalized]
    if normalized:
        candidates.append(normalized[0])
    for candidate in candidates:
        filename = "-".join(f"{ord(char):x}" for char in candidate) + ".svg"
        path = os.path.join(_emoji_svg_dir, filename)
        if not os.path.isfile(path):
            continue
        key = (path, size)
        if key in _emoji_svg_cache:
            return _emoji_svg_cache[key].copy()
        try:
            cairosvg = _get_cairosvg()
            if cairosvg is None:
                return None
            png = cairosvg.svg2png(url=path, output_width=size, output_height=size)
            image = Image.open(BytesIO(png)).convert("RGBA")
            _emoji_svg_cache[key] = image
            return image.copy()
        except Exception:
            return None
    return None


def img_to_rgb565(img):
    """Convert PIL RGBA image to raw RGB565 bytes, little-endian."""
    if np is not None:
        pixels = np.asarray(img, dtype=np.uint8)
        red = pixels[:, :, 0].astype(np.uint16)
        green = pixels[:, :, 1].astype(np.uint16)
        blue = pixels[:, :, 2].astype(np.uint16)
        packed = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
        if pixels.shape[2] > 3:
            packed[pixels[:, :, 3] < 128] = 0
        return packed.astype("<u2", copy=False).tobytes()

    # Compatibility fallback for environments without NumPy. This path is
    # much slower and is not used by the APPLaunch package.
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


# ---- framebuffer device ----
_fb_fd = None
_fb_size = 0
_last_frame_key = None
_last_static_frame_key = None

# ---- line-wrap state ----
_line_wrap_lines = []
_line_wrap_line = 0
_line_wrap_next_ts = 0.0
_line_wrap_current_text = ""
_LINE_SHOW_S = 1.6
_LINE_EXTEND_S = 0.3


def fb_open():
    global _fb_fd, _fb_size
    if SNAPSHOT_PATH:
        return
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


def _state_title(status):
    return {
        "BINDING": "Binding",
        "IDLE": "Idle",
        "LISTENING": "Listening",
        "THINKING": "Thinking",
        "SPEAKING": "Speaking",
        "ERROR": "Error",
    }.get(status, "Idle")


def _draw_emoji(img, draw, emoji, target, x, y):
    emoji = _normalize_emoji(emoji)
    svg_emoji = _emoji_svg_image(emoji, target)
    if svg_emoji is not None:
        img.alpha_composite(svg_emoji, (x, y))
        return svg_emoji.width, svg_emoji.height
    if _emoji_use_embedded:
        scratch = Image.new("RGBA", (160, 160), (0, 0, 0, 0))
        sdraw = ImageDraw.Draw(scratch, "RGBA")
        sdraw.text((0, 0), emoji, font=_emoji_font, embedded_color=True)
        bbox = scratch.getbbox()
        if bbox is not None:
            glyph = scratch.crop(bbox)
            glyph.thumbnail((target, target), Image.Resampling.NEAREST)
            img.alpha_composite(glyph, (x, y))
            return glyph.width, glyph.height
    draw.text((x, y), emoji, font=_emoji_font, fill=(255, 255, 255, 255))
    bbox = _emoji_font.getbbox(emoji)
    return max(1, bbox[2] - bbox[0]), max(1, bbox[3] - bbox[1])


def _watercolor_image(status, now):
    global _watercolor_phase, _watercolor_clock, _watercolor_last_frame
    elapsed = min(0.10, max(0.0, now - _watercolor_clock))
    _watercolor_clock = now
    _watercolor_phase += elapsed
    _watercolor_last_frame = now

    role = "assistant" if status == "SPEAKING" else "user"
    features = _audio_features[role]
    decay = math.exp(-max(0.0, now - features["updated_at"]) * 5.0)
    level = features["level"] * decay
    peak = features["peak"] * decay
    bands = tuple(value * decay for value in features["bands"])
    idle = 0.025 * (0.55 + 0.45 * math.sin(_watercolor_phase * 0.85))
    if status == "SPEAKING":
        pigment_level = max(idle, level)
        pigment_bands = bands
        visual_scale = 1.0
    else:
        pigment_level = idle
        pigment_bands = [0.0, 0.0, 0.0, 0.0]
        visual_scale = (
            1.0 + min(0.13, level * 0.10 + peak * 0.035)
            if status == "LISTENING"
            else 1.0
        )

    packed = _watercolor.rgb565(
        _watercolor_phase,
        pigment_level,
        peak,
        pigment_bands,
        tuple(_audio_features["assistant"]["cumulative"]),
        visual_scale,
        None,
    )
    # Whisplay's SPI renderer returns network-order RGB565. CardputerZero's
    # framebuffer and Pillow's BGR;16 decoder use little-endian packed words.
    words = array.array("H")
    words.frombytes(packed)
    if sys.byteorder == "little":
        words.byteswap()
    return Image.frombytes(
        "RGB",
        (WATERCOLOR_PANE_WIDTH, WATERCOLOR_PANE_HEIGHT),
        words.tobytes(),
        "raw",
        "BGR;16",
    ).convert("RGBA")


def render_frame(status, emoji, text, code, terminal):
    """Render a full frame and write to framebuffer."""
    global _last_frame_key, _last_static_frame_key
    global _line_wrap_lines, _line_wrap_line, _line_wrap_next_ts, _line_wrap_current_text
    now = time.monotonic()
    static_key = (status, emoji, text, code, terminal)
    if (
        _watercolor is not None
        and static_key == _last_static_frame_key
        and now - _watercolor_last_frame < 1.0 / WATERCOLOR_FPS
    ):
        return
    _last_static_frame_key = static_key

    img = Image.new("RGBA", (WIDTH, HEIGHT), (0, 0, 0, 255))
    orb_x = WIDTH
    if _watercolor is not None:
        orb_x = WIDTH - WATERCOLOR_PANE_WIDTH
        img.alpha_composite(_watercolor_image(status, now), (orb_x, 0))
    draw = ImageDraw.Draw(img, "RGBA")

    # The top bar shares the black canvas; state is part of the title instead
    # of being repeated in a coloured band below it.
    draw.text(
        (8, 5),
        f"Xiaozhi - {_state_title(status)}",
        font=_status_font,
        fill=(245, 245, 248, 255),
    )

    emoji = _normalize_emoji(emoji)
    if _watercolor is not None:
        emoji_target = 28
        emoji_x = orb_x - 3
        emoji_y = HEIGHT - 38 - emoji_target - 9
    else:
        emoji_target = 40
        emoji_x = WIDTH - emoji_target - 8
        emoji_y = 37
    _draw_emoji(img, draw, emoji, emoji_target, emoji_x, emoji_y)

    # Tool output replaces the normal status/prompt text while the emoji stays
    # in its original position on the right.
    if terminal:
        terminal_x = 8
        terminal_y = 34
        max_width = (
            orb_x - terminal_x - 12
            if _watercolor is not None
            else WIDTH - terminal_x - emoji_target - 24
        )
        raw_lines = terminal.replace("\r\n", "\n").replace("\r", "\n").split("\n")
        lines = [line for line in raw_lines if line][-6:]
        line_height = 11
        for index, line in enumerate(lines):
            clipped = line
            while clipped and _terminal_font.getlength(clipped) > max_width:
                clipped = clipped[:-1]
            if clipped != line and len(clipped) > 3:
                clipped = clipped[:-3] + "..."
            draw.text(
                (terminal_x, terminal_y + index * line_height),
                clipped,
                font=_terminal_font,
                fill=(80, 255, 120, 255),
            )
    # Content area
    if terminal:
        pass
    elif status == "BINDING":
        draw.text((8, 39), "Go to xiaozhi.me to bind", font=_small_font, fill=(155, 160, 180, 255))
        if code and len(code) == 6:
            bbox = _code_font.getbbox(code)
            cw = bbox[2] - bbox[0]
            content_width = orb_x if _watercolor is not None else WIDTH
            draw.text(((content_width - cw) // 2, 72), code, font=_code_font, fill=(235, 240, 255, 255))
    elif status == "IDLE":
        draw.text((8, 39), "SPACE to talk", font=_small_font, fill=(155, 160, 180, 255))
        draw.text(
            (8, 58),
            "O: switch display mode",
            font=_small_font,
            fill=(105, 112, 132, 255),
        )
    elif status == "LISTENING":
        draw.text((8, 39), "Listening...", font=_small_font, fill=(155, 160, 180, 255))
    elif status == "THINKING":
        draw.text((8, 39), "Thinking...", font=_small_font, fill=(155, 160, 180, 255))
    elif status == "SPEAKING":
        draw.text((8, 39), "Speaking...", font=_small_font, fill=(155, 160, 180, 255))
    elif status == "ERROR":
        draw.text((8, 39), "ERROR", font=_small_font, fill=(200, 40, 40, 255))

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
                _line_wrap_lines = _wrap_text(text, _text_font, avail_w)
                _line_wrap_current_text = text
                # A new TTS sentence must become visible immediately instead
                # of inheriting the scroll position of the previous sentence.
                _line_wrap_line = 0
                _line_wrap_next_ts = time.monotonic() + _LINE_SHOW_S + _LINE_EXTEND_S
            else:
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

    frame_key = (status, emoji, text, code, terminal, line_bucket)
    if _watercolor is None and frame_key == _last_frame_key:
        img.close()
        return
    _last_frame_key = frame_key

    if SNAPSHOT_PATH:
        img.save(SNAPSHOT_PATH, format="PNG")

    # Write to framebuffer
    data = img_to_rgb565(img)
    fb_write(data)

    img.close()


def main():
    global _fb_fd, _watercolor, _last_frame_key, _last_static_frame_key
    fb_open()
    print(json.dumps({"event": "connected"}), flush=True)

    current = {
        "status": "IDLE",
        "emoji": "😄",
        "text": "",
        "code": "",
        "terminal": "",
        "has_frame": False,
    }

    while True:
        timeout = min(0.08, 1.0 / WATERCOLOR_FPS) if _watercolor is not None else 0.08
        readable, _, _ = select.select([sys.stdin], [], [], timeout)
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

            if cmd.get("cmd") == "toggle_style":
                if _watercolor is not None:
                    _watercolor = None
                    style = "classic"
                else:
                    try:
                        _watercolor = _load_watercolor_renderer(force=True)
                        style = "watercolor"
                    except Exception as exc:
                        print(json.dumps({"event": "error", "text": str(exc)}), flush=True)
                        continue
                _last_frame_key = None
                _last_static_frame_key = None
                print(json.dumps({"event": "display_style", "style": style}), flush=True)
                continue

            if cmd.get("cmd") == "audio":
                role = cmd.get("role", "user")
                _update_audio_features(
                    cmd.get("pcm", ""),
                    cmd.get("sample_rate", 16000),
                    role,
                )
                continue

            if cmd.get("cmd") == "render":
                try:
                    current["status"] = cmd.get("status", "IDLE")
                    current["emoji"] = cmd.get("emoji", "😄")
                    current["text"] = cmd.get("text", "")
                    current["code"] = cmd.get("code", "")
                    current["terminal"] = cmd.get("terminal", "")
                    current["has_frame"] = True
                except Exception:
                    # write to stdout (NOT stderr) to allow C++ drain thread to consume it
                    traceback.print_exc()

        if current["has_frame"]:
            try:
                render_frame(
                    current["status"],
                    current["emoji"],
                    current["text"],
                    current["code"],
                    current["terminal"],
                )
            except Exception:
                traceback.print_exc(file=sys.stderr)

    if _fb_fd is not None:
        os.close(_fb_fd)
        _fb_fd = None


if __name__ == "__main__":
    main()
