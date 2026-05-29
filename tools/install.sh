#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# Bundled fonts may be next to install.sh (CI artifact) or inside .deb install path
BUNDLED_FONTS=""
for d in "$SCRIPT_DIR/fonts" "/usr/share/APPLaunch/share/xiaozhi/fonts"; do
    if [ -d "$d" ] && [ -n "$(ls -A "$d" 2>/dev/null)" ]; then
        BUNDLED_FONTS="$d"
        break
    fi
done
FONT_DIR="/usr/share/fonts/truetype/xiaozhi"
VENDOR_DIR=""
for d in "$SCRIPT_DIR/../main/tools/vendor" "$SCRIPT_DIR/vendor" "/usr/share/APPLaunch/share/xiaozhi/vendor"; do
    if [ -d "$d/opuslib" ]; then
        VENDOR_DIR="$d"
        break
    fi
done

echo "=== XiaoZhi App Launcher installer ==="

# ── system packages ──────────────────────────────────────────────
echo "[1/4] Installing system packages..."
SYSTEM_PKGS="libsdl2-dev libsdl2-ttf-dev libopus0 python3 python3-pil python3-websockets fonts-noto-cjk fonts-noto-color-emoji"
NEED_INSTALL=""

for pkg in $SYSTEM_PKGS; do
    if ! dpkg -s "$pkg" >/dev/null 2>&1; then
        NEED_INSTALL="$NEED_INSTALL $pkg"
    fi
done

if [ -n "$NEED_INSTALL" ]; then
    echo "  → installing:$NEED_INSTALL"
    if [ "$(id -u)" -eq 0 ]; then
        apt-get update -qq && apt-get install -y $NEED_INSTALL
    else
        sudo apt-get update -qq && sudo apt-get install -y $NEED_INSTALL
    fi
else
    echo "  ✓ all system packages present"
fi

# ── Python dependencies ────────────────────────────────────────
echo "[2/4] Verifying Python dependencies..."
if python3 -c "import websockets" 2>/dev/null; then
    echo "  ✓ websockets"
else
    echo "  ✗ websockets missing (expected from system package: python3-websockets)"
    exit 1
fi

if [ -z "$VENDOR_DIR" ]; then
    echo "  ✗ vendored opuslib directory not found"
    exit 1
fi

if python3 -c "import sys; sys.path.insert(0, '$VENDOR_DIR'); import opuslib" 2>/dev/null; then
    echo "  ✓ opuslib (vendored)"
else
    echo "  ✗ vendored opuslib import failed from $VENDOR_DIR"
    exit 1
fi

# ── Fonts ────────────────────────────────────────────────────────
echo "[3/4] Checking fonts..."
# The display_bridge.py searches these paths in order.
FONT_OK=0
for candidate in \
    "$FONT_DIR/NotoSansSC-Regular.ttf" \
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc" \
    "/usr/share/fonts/opentype/noto/NotoSansCJKSC-Regular.otf" \
    "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc" \
    "/usr/share/fonts/truetype/noto/NotoSansSC-Regular.ttf" \
    "/usr/share/fonts/truetype/noto/NotoSansSC-Regular.otf" \
    "$FONT_DIR/NotoSansSC-Bold.ttf" \
    ${BUNDLED_FONTS:+"$BUNDLED_FONTS/NotoSansSC-Regular.ttf"} \
    ${BUNDLED_FONTS:+"$BUNDLED_FONTS/NotoSansSC-Bold.ttf"}; do
    if [ -n "$candidate" ] && [ -f "$candidate" ]; then
        FONT_OK=1
        echo "  ✓ CJK font: $candidate"
        break
    fi
done

EMOJI_OK=0
for candidate in \
    "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf" \
    "/usr/share/fonts/opentype/noto/NotoColorEmoji.ttf" \
    "$FONT_DIR/NotoColorEmoji.ttf" \
    ${BUNDLED_FONTS:+"$BUNDLED_FONTS/NotoColorEmoji.ttf"}; do
    if [ -n "$candidate" ] && [ -f "$candidate" ]; then
        EMOJI_OK=1
        echo "  ✓ Emoji font: $candidate"
        break
    fi
done

if [ "$FONT_OK" -eq 0 ] || [ "$EMOJI_OK" -eq 0 ]; then
    echo "  → installing fonts to $FONT_DIR ..."
    mkdir -p "$FONT_DIR"

    if [ -n "${BUNDLED_FONTS:-}" ] && [ -f "$BUNDLED_FONTS/NotoSansSC-Regular.ttf" ]; then
        echo "  → using bundled NotoSansSC-Regular.ttf"
        cp "$BUNDLED_FONTS/NotoSansSC-Regular.ttf" "$FONT_DIR/"
    fi

    if [ "$FONT_OK" -eq 0 ]; then
        if [ -n "${BUNDLED_FONTS:-}" ] && [ -f "$BUNDLED_FONTS/NotoSansSC-Bold.ttf" ]; then
            echo "  → using bundled CJK font"
            cp "$BUNDLED_FONTS/NotoSansSC-Bold.ttf" "$FONT_DIR/"
        else
            echo "  → fetching NotoSansSC-Bold.ttf from Google Fonts..."
            curl -fsSLo "$FONT_DIR/NotoSansSC-Bold.ttf" \
                "https://github.com/google/fonts/raw/main/ofl/notosanssc/static/NotoSansSC-Bold.ttf" 2>/dev/null || {
                echo "  ⚠ font download failed, trying apt fallback..."
                if [ "$(id -u)" -eq 0 ]; then
                    apt-get install -y fonts-noto-cjk 2>/dev/null || true
                else
                    sudo apt-get install -y fonts-noto-cjk 2>/dev/null || true
                fi
            }
        fi
    fi

    if [ "$EMOJI_OK" -eq 0 ]; then
        if [ -n "${BUNDLED_FONTS:-}" ] && [ -f "$BUNDLED_FONTS/NotoColorEmoji.ttf" ]; then
            echo "  → using bundled emoji font"
            cp "$BUNDLED_FONTS/NotoColorEmoji.ttf" "$FONT_DIR/"
        else
            echo "  → fetching NotoColorEmoji.ttf from Google Fonts..."
            curl -fsSLo "$FONT_DIR/NotoColorEmoji.ttf" \
                "https://github.com/google/fonts/raw/main/ofl/notocoloremoji/NotoColorEmoji%5Bwght%5D.ttf" 2>/dev/null || {
                echo "  ⚠ emoji font download failed, trying apt fallback..."
                if [ "$(id -u)" -eq 0 ]; then
                    apt-get install -y fonts-noto-color-emoji 2>/dev/null || true
                else
                    sudo apt-get install -y fonts-noto-color-emoji 2>/dev/null || true
                fi
            }
        fi
    fi

    # Update font cache
    if command -v fc-cache >/dev/null 2>&1; then
        fc-cache -f "$FONT_DIR" 2>/dev/null || true
    fi
else
    echo "  ✓ all fonts present"
fi

# ── Verify ───────────────────────────────────────────────────────
echo "[4/4] Verifying installation..."
echo "  ✓ xiaozhi_app: $(which xiaozhi_app 2>/dev/null || echo /usr/share/APPLaunch/bin/xiaozhi_app)"

# ── Ensure PipeWire is present for audio ──
echo "  → installing PipeWire audio..."
if [ "$(id -u)" -eq 0 ]; then
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        pipewire pipewire-pulse wireplumber 2>/dev/null || true
else
    sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        pipewire pipewire-pulse wireplumber 2>/dev/null || true
fi
echo "  ✓ PipeWire ready"

# ── ES8388 mixer: boost mic gain and output volume ──
echo "  → initializing ES8388 mixer..."
amixer -c 1 sset 'ADC MUX' AMIC 2>/dev/null || true
amixer -c 1 sset 'PGAL Select' 'DifferentialL' 2>/dev/null || true
amixer -c 1 sset 'PGAR Select' 'DifferentialR' 2>/dev/null || true
amixer -c 1 sset 'ADCL' 220 2>/dev/null || true
amixer -c 1 sset 'ADCR' 220 2>/dev/null || true
amixer -c 1 sset 'ADCL PGA' 14 2>/dev/null || true
amixer -c 1 sset 'ADCR PGA' 14 2>/dev/null || true
amixer -c 1 sset 'DACL' 200 2>/dev/null || true
amixer -c 1 sset 'DACR' 200 2>/dev/null || true
amixer -c 1 sset 'ADC2DAC Mixer' 0 2>/dev/null || true
amixer -c 1 sset 'OUTL MUX' Normal 2>/dev/null || true
amixer -c 1 sset 'OUTR MUX' Normal 2>/dev/null || true

# Unmute PipeWire default sink/source and set them to 100%
if command -v wpctl >/dev/null 2>&1; then
    wpctl set-mute @DEFAULT_AUDIO_SINK@ 0 2>/dev/null || true
    wpctl set-volume @DEFAULT_AUDIO_SINK@ 0.65 2>/dev/null || true
    wpctl set-mute @DEFAULT_AUDIO_SOURCE@ 0 2>/dev/null || true
    wpctl set-volume @DEFAULT_AUDIO_SOURCE@ 1.0 2>/dev/null || true
fi
echo "  ✓ ES8388 mixer configured"
echo ""
echo "=== Installation complete ==="
echo ""
echo "To start:  sudo systemctl restart APPLaunch.service"
echo "Or run:    /usr/share/APPLaunch/bin/xiaozhi_launcher"
