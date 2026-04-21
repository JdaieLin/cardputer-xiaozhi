#include "ui_sdl.hpp"

#include <array>
#include <cctype>
#include <iostream>
#include <regex>
#include <unordered_map>

namespace xiaozhi {
namespace {

using Glyph = std::array<unsigned char, 7>;

const std::unordered_map<char, Glyph>& glyphs() {
    static const std::unordered_map<char, Glyph> kGlyphs = {
        {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
        {'-', {0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00}},
        {':', {0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00}},
        {'/', {0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00}},
        {'0', {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e}},
        {'1', {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e}},
        {'2', {0x0e, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1f}},
        {'3', {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e}},
        {'4', {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02}},
        {'5', {0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e}},
        {'6', {0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e}},
        {'7', {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
        {'8', {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e}},
        {'9', {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c}},
        {'A', {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}},
        {'B', {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e}},
        {'C', {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e}},
        {'D', {0x1c, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1c}},
        {'E', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f}},
        {'F', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10}},
        {'G', {0x0e, 0x11, 0x10, 0x10, 0x13, 0x11, 0x0f}},
        {'H', {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}},
        {'I', {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e}},
        {'J', {0x1f, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0c}},
        {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
        {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f}},
        {'M', {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11}},
        {'N', {0x11, 0x19, 0x19, 0x15, 0x13, 0x13, 0x11}},
        {'O', {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}},
        {'P', {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10}},
        {'Q', {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d}},
        {'R', {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11}},
        {'S', {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e}},
        {'T', {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
        {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}},
        {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04}},
        {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a}},
        {'X', {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11}},
        {'Y', {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04}},
        {'Z', {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f}},
    };
    return kGlyphs;
}

void drawText(SDL_Renderer* renderer, int x, int y, int scale, const std::string& text) {
    int cursor_x = x;
    const auto& font = glyphs();

    for (char ch : text) {
        const char up = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        const auto it = font.find(up);
        const Glyph glyph = (it == font.end()) ? font.at(' ') : it->second;

        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if ((glyph[row] & (1 << (4 - col))) == 0) {
                    continue;
                }
                SDL_Rect pixel = {cursor_x + col * scale, y + row * scale, scale, scale};
                SDL_RenderFillRect(renderer, &pixel);
            }
        }
        cursor_x += 6 * scale;
    }
}

std::string extractCode(const std::string& text) {
    std::smatch match;
    static const std::regex code_regex("([0-9]{6})");
    if (std::regex_search(text, match, code_regex)) {
        return match[1].str();
    }
    return "------";
}

}  // namespace

UiSdl::~UiSdl() {
    if (renderer_ != nullptr) {
        SDL_DestroyRenderer(renderer_);
    }
    if (window_ != nullptr) {
        SDL_DestroyWindow(window_);
    }
}

bool UiSdl::init() {
    window_ = SDL_CreateWindow("cardputer-xiaozhi simulator",
                               SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED,
                               480,
                               280,
                               SDL_WINDOW_SHOWN);
    if (window_ == nullptr) {
        std::cerr << "[ui-sdl] SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        return false;
    }

    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer_ == nullptr) {
        std::cerr << "[ui-sdl] SDL_CreateRenderer failed: " << SDL_GetError() << std::endl;
        return false;
    }

    std::cout << "[ui-sdl] initialized" << std::endl;
    return true;
}

void UiSdl::renderState(AppState state, const std::string& text) {
    if (renderer_ == nullptr || window_ == nullptr) {
        return;
    }

    std::array<unsigned char, 3> color{30, 30, 30};
    switch (state) {
        case AppState::Binding:
            color = {30, 50, 95};
            break;
        case AppState::Idle:
            color = {35, 80, 35};
            break;
        case AppState::Listening:
            color = {25, 85, 130};
            break;
        case AppState::Thinking:
            color = {120, 100, 20};
            break;
        case AppState::Speaking:
            color = {110, 40, 90};
            break;
        case AppState::Error:
            color = {130, 25, 25};
            break;
    }

    SDL_SetRenderDrawColor(renderer_, color[0], color[1], color[2], 255);
    SDL_RenderClear(renderer_);

    SDL_Rect panel = {14, 14, 452, 190};
    SDL_SetRenderDrawColor(renderer_, 18, 18, 18, 220);
    SDL_RenderFillRect(renderer_, &panel);

    SDL_SetRenderDrawColor(renderer_, 210, 210, 210, 255);
    drawText(renderer_, 28, 28, 3, "CARDPUTER XIAOZHI");
    drawText(renderer_, 28, 62, 2, std::string("STATE: ") + stateName(state));
    if (state == AppState::Binding) {
        drawText(renderer_, 28, 88, 2, "OPEN XIAOZHI APP AND BIND");
        drawText(renderer_, 28, 118, 3, std::string("CODE: ") + extractCode(text));
    } else {
        drawText(renderer_, 28, 88, 2, "SPACE: PUSH TO TALK");
    }

    SDL_Rect status = {20, 220, 440, 40};
    SDL_SetRenderDrawColor(renderer_, 235, 235, 235, 255);
    SDL_RenderFillRect(renderer_, &status);

    SDL_SetRenderDrawColor(renderer_, 15, 15, 15, 255);
    drawText(renderer_, 28, 232, 2, std::string("MSG: ") + text.substr(0, 35));

    SDL_RenderPresent(renderer_);

    std::string title = std::string("cardputer-xiaozhi | ") + stateName(state) + " | " + text + " | SPACE=PTT";
    SDL_SetWindowTitle(window_, title.c_str());
}

const char* UiSdl::stateName(AppState state) const {
    switch (state) {
        case AppState::Binding:
            return "BINDING";
        case AppState::Idle:
            return "IDLE";
        case AppState::Listening:
            return "LISTENING";
        case AppState::Thinking:
            return "THINKING";
        case AppState::Speaking:
            return "SPEAKING";
        case AppState::Error:
            return "ERROR";
    }
    return "UNKNOWN";
}

}  // namespace xiaozhi
