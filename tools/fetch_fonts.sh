#!/usr/bin/env bash
set -euo pipefail

FONTS_DIR="${1:-fonts}"
mkdir -p "$FONTS_DIR"

echo "=== Fetching fonts to $FONTS_DIR ==="

# Noto Sans CJK SC Regular (CJK text)
CJK_URL="https://raw.githubusercontent.com/notofonts/noto-cjk/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf"
CJK_FILE="$FONTS_DIR/NotoSansCJKsc-Regular.otf"

if [ ! -f "$CJK_FILE" ]; then
    echo "  → $CJK_URL"
    curl -fsSLo "$CJK_FILE" "$CJK_URL" || {
        echo "  ⚠ primary URL failed, trying mirror..."
        curl -fsSLo "$CJK_FILE" \
            "https://github.com/notofonts/noto-cjk/raw/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf" || {
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
EMOJI_URL="https://raw.githubusercontent.com/googlefonts/noto-emoji/main/fonts/NotoColorEmoji.ttf"
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
