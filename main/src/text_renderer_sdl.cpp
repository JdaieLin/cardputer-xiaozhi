#include "text_renderer_sdl.hpp"

#include <SDL_ttf.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace xiaozhi {
namespace {

bool ensureTtfInit() {
    if (TTF_WasInit() != 0) {
        return true;
    }
    return TTF_Init() == 0;
}

uint32_t nextCodepoint(const std::string& s, size_t& i) {
    if (i >= s.size()) {
        return 0;
    }
    const unsigned char c0 = static_cast<unsigned char>(s[i++]);
    if ((c0 & 0x80) == 0) {
        return c0;
    }
    if ((c0 & 0xE0) == 0xC0 && i < s.size()) {
        const unsigned char c1 = static_cast<unsigned char>(s[i++]);
        return ((c0 & 0x1F) << 6) | (c1 & 0x3F);
    }
    if ((c0 & 0xF0) == 0xE0 && i + 1 < s.size()) {
        const unsigned char c1 = static_cast<unsigned char>(s[i++]);
        const unsigned char c2 = static_cast<unsigned char>(s[i++]);
        return ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
    }
    if ((c0 & 0xF8) == 0xF0 && i + 2 < s.size()) {
        const unsigned char c1 = static_cast<unsigned char>(s[i++]);
        const unsigned char c2 = static_cast<unsigned char>(s[i++]);
        const unsigned char c3 = static_cast<unsigned char>(s[i++]);
        return ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
    }
    return '?';
}

bool hasEmojiCodepoint(const std::string& text) {
    size_t i = 0;
    while (i < text.size()) {
        const uint32_t cp = nextCodepoint(text, i);
        if ((cp >= 0x1F300 && cp <= 0x1FAFF) || (cp >= 0x2600 && cp <= 0x27BF)) {
            return true;
        }
    }
    return false;
}

struct FontKey {
    std::string path;
    int size = 0;
    int style = 0;
    bool operator<(const FontKey& other) const {
        if (path != other.path) return path < other.path;
        if (size != other.size) return size < other.size;
        return style < other.style;
    }
};

TTF_Font* getFont(const std::vector<std::string>& paths, int px, bool bold) {
    static std::map<FontKey, TTF_Font*> cache;
    const int style = bold ? TTF_STYLE_BOLD : TTF_STYLE_NORMAL;

    for (const std::string& path : paths) {
        FontKey key{path, px, style};
        auto it = cache.find(key);
        if (it != cache.end()) {
            return it->second;
        }

        TTF_Font* font = TTF_OpenFont(path.c_str(), px);
        if (font == nullptr) {
            continue;
        }
        TTF_SetFontStyle(font, style);
        cache.emplace(key, font);
        return font;
    }
    return nullptr;
}

std::vector<std::string> normalFontCandidates() {
    return {
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJKSC-Regular.otf",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansSC-Regular.otf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    };
}

std::vector<std::string> emojiFontCandidates() {
    return {
        "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf",
        "/usr/share/fonts/truetype/ancient-scripts/Symbola_hint.ttf",
    };
}

}  // namespace

bool drawSdlText(SDL_Renderer* renderer, const SDL_Rect& rect, const std::string& text, const TextStyle& style) {
    if (renderer == nullptr || rect.w <= 0 || rect.h <= 0 || text.empty()) {
        return false;
    }
    if (!ensureTtfInit()) {
        return false;
    }

    const int point_size = std::max(10, static_cast<int>(style.point_size));
    TTF_Font* font = nullptr;
    if (hasEmojiCodepoint(text)) {
        font = getFont(emojiFontCandidates(), point_size, style.bold);
    }
    if (font == nullptr) {
        font = getFont(normalFontCandidates(), point_size, style.bold);
    }
    if (font == nullptr) {
        return false;
    }

    SDL_Surface* surface = nullptr;
    if (style.wrap) {
        surface = TTF_RenderUTF8_Blended_Wrapped(font, text.c_str(), style.color, static_cast<Uint32>(rect.w));
    } else {
        surface = TTF_RenderUTF8_Blended(font, text.c_str(), style.color);
    }
    if (surface == nullptr) {
        return false;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture == nullptr) {
        SDL_FreeSurface(surface);
        return false;
    }

    SDL_Rect dst = {rect.x, rect.y, surface->w, surface->h};
    if (style.center) {
        dst.x = rect.x + std::max(0, (rect.w - surface->w) / 2);
    }
    dst.y = rect.y + std::max(0, (rect.h - surface->h) / 2);
    dst.w = std::min(dst.w, rect.w);
    dst.h = std::min(dst.h, rect.h);

    SDL_RenderCopy(renderer, texture, nullptr, &dst);
    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
    return true;
}

}  // namespace xiaozhi
