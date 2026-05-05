#pragma once

#include <string>

#include <SDL.h>

namespace xiaozhi {

struct TextStyle {
    SDL_Color color{255, 255, 255, 255};
    float point_size = 18.0f;
    bool bold = false;
    bool center = false;
    bool wrap = false;
};

bool drawSdlText(SDL_Renderer* renderer, const SDL_Rect& rect, const std::string& text, const TextStyle& style);

}  // namespace xiaozhi
