#!/usr/bin/env bash
set -euo pipefail

FONTS_DIR="${1:-fonts}"
mkdir -p "$FONTS_DIR"

echo "=== Fetching fonts to $FONTS_DIR ==="

# NotoSansSC Bold (CJK text, ~5 MB)
CJK_URL="https://github.com/google/fonts/raw/main/ofl/notosanssc/static/NotoSansSC-Bold.ttf"
CJK_FILE="$FONTS_DIR/NotoSansSC-Bold.ttf"

if [ ! -f "$CJK_FILE" ]; then
    echo "  → $CJK_URL"
    curl -fsSLo "$CJK_FILE" "$CJK_URL" || {
        echo "  ⚠ primary URL failed, trying mirror..."
        curl -fsSLo "$CJK_FILE" \
            "https://raw.githubusercontent.com/notofonts/noto-cjk/main/Sans/OTF/SimplifiedChinese/NotoSansSC-Bold.otf" || {
            echo "  ⚠ mirror also failed, skipping CJK font"
            rm -f "$CJK_FILE"
        }
    }
    if [ -f "$CJK_FILE" ]; then
        echo "  ✓ $(du -h "$CJK_FILE" | cut -f1)  $CJK_FILE"
    fi
else
    echo "  ✓ (cached) $CJK_FILE"
fi

# NotoColorEmoji (~10 MB)
EMOJI_URL="https://github.com/google/fonts/raw/main/ofl/notocoloremoji/NotoColorEmoji%5Bwght%5D.ttf"
EMOJI_FILE="$FONTS_DIR/NotoColorEmoji.ttf"

if [ ! -f "$EMOJI_FILE" ]; then
    echo "  → $EMOJI_URL"
    curl -fsSLo "$EMOJI_FILE" "$EMOJI_URL" || {
        echo "  ⚠ emoji font download failed"
        rm -f "$EMOJI_FILE"
    }
    if [ -f "$EMOJI_FILE" ]; then
        echo "  ✓ $(du -h "$EMOJI_FILE" | cut -f1)  $EMOJI_FILE"
    fi
else
    echo "  ✓ (cached) $EMOJI_FILE"
fi

echo "=== Font fetch complete ==="
ls -lh "$FONTS_DIR/" 2>/dev/null || true
