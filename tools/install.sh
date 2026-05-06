#!/usr/bin/env bash
set -euo pipefail

FONT_URL_BASE="https://github.com/JdaieLin/cardputer-xiaozhi/releases/download/fonts"
FONT_DIR="/usr/share/fonts/truetype/xiaozhi"

echo "=== XiaoZhi App Launcher installer ==="

# ── system packages ──────────────────────────────────────────────
echo "[1/4] Installing system packages..."
SYSTEM_PKGS="libsdl2-2.0-0 libsdl2-ttf-2.0-0 python3 python3-pip python3-pil"
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

# ── Python packages ─────────────────────────────────────────────
echo "[2/4] Installing Python packages..."
PYTHON_PKGS="websockets opuslib"
for pkg in $PYTHON_PKGS; do
    if ! python3 -c "import $pkg" 2>/dev/null; then
        echo "  → installing: $pkg"
        if [ "$(id -u)" -eq 0 ]; then
            pip3 install --break-system-packages "$pkg"
        else
            sudo pip3 install --break-system-packages "$pkg"
        fi
    else
        echo "  ✓ $pkg"
    fi
done

# ── Fonts ────────────────────────────────────────────────────────
echo "[3/4] Checking fonts..."
# The display_bridge.py searches these paths in order.
FONT_OK=0
for candidate in \
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc" \
    "/usr/share/fonts/opentype/noto/NotoSansCJKSC-Regular.otf" \
    "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc" \
    "/usr/share/fonts/truetype/noto/NotoSansSC-Regular.otf" \
    "/usr/share/fonts/truetype/xiaozhi/NotoSansSC-Bold.ttf"; do
    if [ -f "$candidate" ]; then
        FONT_OK=1
        echo "  ✓ CJK font: $candidate"
        break
    fi
done

EMOJI_OK=0
for candidate in \
    "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf" \
    "/usr/share/fonts/opentype/noto/NotoColorEmoji.ttf" \
    "/usr/share/fonts/truetype/xiaozhi/NotoColorEmoji.ttf"; do
    if [ -f "$candidate" ]; then
        EMOJI_OK=1
        echo "  ✓ Emoji font: $candidate"
        break
    fi
done

if [ "$FONT_OK" -eq 0 ] || [ "$EMOJI_OK" -eq 0 ]; then
    echo "  → downloading minimal fonts to $FONT_DIR ..."
    mkdir -p "$FONT_DIR"

    if [ "$FONT_OK" -eq 0 ]; then
        echo "  → fetching NotoSansSC-Bold.ttf ..."
        curl -fsSLo "$FONT_DIR/NotoSansSC-Bold.ttf" \
            "$FONT_URL_BASE/NotoSansSC-Bold.ttf" 2>/dev/null || {
            echo "  ⚠ font download failed, trying apt fallback..."
            if [ "$(id -u)" -eq 0 ]; then
                apt-get install -y fonts-noto-cjk 2>/dev/null || true
            else
                sudo apt-get install -y fonts-noto-cjk 2>/dev/null || true
            fi
        }
    fi

    if [ "$EMOJI_OK" -eq 0 ]; then
        echo "  → fetching NotoColorEmoji.ttf ..."
        curl -fsSLo "$FONT_DIR/NotoColorEmoji.ttf" \
            "$FONT_URL_BASE/NotoColorEmoji.ttf" 2>/dev/null || {
            echo "  ⚠ emoji font download failed, trying apt fallback..."
            if [ "$(id -u)" -eq 0 ]; then
                apt-get install -y fonts-noto-color-emoji 2>/dev/null || true
            else
                sudo apt-get install -y fonts-noto-color-emoji 2>/dev/null || true
            fi
        }
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
echo ""
echo "=== Installation complete ==="
echo ""
echo "To start:  sudo systemctl restart APPLaunch.service"
echo "Or run:    /usr/share/APPLaunch/bin/xiaozhi_launcher"
