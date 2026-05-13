#include "ui_sdl.hpp"

#include <cstdlib>
#include <iostream>
#include <array>
#include <regex>
#include <vector>

#include "hal_sdl.hpp"
#include "text_renderer_sdl.hpp"

namespace xiaozhi {
namespace {

std::string extractCode(const std::string& text) {
    std::smatch match;
    static const std::regex code_regex("([0-9]{6})");
    if (std::regex_search(text, match, code_regex)) {
        return match[1].str();
    }
    return "------";
}

std::vector<std::string> splitUtf8Glyphs(const std::string& text) {
    std::vector<std::string> glyphs;
    for (size_t i = 0; i < text.size();) {
        unsigned char lead = static_cast<unsigned char>(text[i]);
        size_t width = 1;
        if ((lead & 0xE0) == 0xC0) {
            width = 2;
        } else if ((lead & 0xF0) == 0xE0) {
            width = 3;
        } else if ((lead & 0xF8) == 0xF0) {
            width = 4;
        }
        width = std::min(width, text.size() - i);
        glyphs.emplace_back(text.substr(i, width));
        i += width;
    }
    return glyphs;
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
    if (const char* path = std::getenv("CARDPUTER_UI_SNAPSHOT_PATH"); path != nullptr) {
        snapshot_path_ = path;
    }
    if (const char* text = std::getenv("CARDPUTER_UI_FORCE_TEXT"); text != nullptr) {
        force_text_ = text;
    }

    if (!sdlVideoOk()) {
        std::cout << "[ui-sdl] video subsystem unavailable, running headless" << std::endl;
        return true;
    }

    window_ = SDL_CreateWindow("cardputer-xiaozhi device",
                               SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED,
                               480,
                               280,
                               SDL_WINDOW_SHOWN);
    if (window_ == nullptr) {
        std::cerr << "[ui-sdl] SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        std::cout << "[ui-sdl] running headless" << std::endl;
        return true;
    }

    Uint32 renderer_flags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC;
    if (!snapshot_path_.empty()) {
        renderer_flags = SDL_RENDERER_SOFTWARE;
    }

    renderer_ = SDL_CreateRenderer(window_, -1, renderer_flags);
    if (renderer_ == nullptr) {
        std::cerr << "[ui-sdl] SDL_CreateRenderer failed: " << SDL_GetError() << std::endl;
        std::cout << "[ui-sdl] running headless" << std::endl;
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        return true;
    }

    std::cout << "[ui-sdl] initialized" << std::endl;
    return true;
}

void UiSdl::renderState(AppState state, const std::string& text, const std::string& emoji) {
    if (renderer_ == nullptr || window_ == nullptr) {
        std::cout << "[ui-sdl] state=" << stateName(state)
                  << " msg=" << text
                  << " emoji=" << emoji << std::endl;
        return;
    }

    const std::string source_text = force_text_.empty() ? text : force_text_;
    const std::string display_text = marqueeText(source_text);

    std::array<unsigned char, 3> color{30, 86, 145};
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

    constexpr int kPanelX = 20;
    constexpr int kPanelY = 14;
    constexpr int kPanelW = 440;
    constexpr int kPanelH = 190;
    constexpr int kPanelPad = 14;
    constexpr int kStatusY = 220;
    constexpr int kStatusH = 40;

    SDL_Rect panel = {kPanelX, kPanelY, kPanelW, kPanelH};
    SDL_SetRenderDrawColor(renderer_, 18, 18, 18, 220);
    SDL_RenderFillRect(renderer_, &panel);

    const SDL_Rect title_rect = {kPanelX + kPanelPad, kPanelY + kPanelPad - 2, 300, 32};
    const SDL_Rect emoji_rect = {kPanelX + kPanelW - kPanelPad - 56, kPanelY + kPanelPad - 6, 56, 56};
    const SDL_Rect state_rect = {kPanelX + kPanelPad, kPanelY + 42, 220, 24};
    const SDL_Rect hint_rect = {kPanelX + kPanelPad, kPanelY + 74, kPanelW - (kPanelPad * 2), 24};
    const SDL_Rect code_rect = {kPanelX + kPanelPad, kPanelY + 102, kPanelW - (kPanelPad * 2), 52};

    drawSdlText(renderer_, title_rect, "Cardputer XiaoZhi", {{235, 235, 235, 255}, 24.0f, true, false, false});
    drawSdlText(renderer_, emoji_rect, emoji.empty() ? stateEmoji(state) : emoji, {{255, 255, 255, 255}, 30.0f, false, true, false});
    drawSdlText(renderer_, state_rect, stateName(state), {{210, 210, 210, 255}, 16.0f, false, false, false});

    if (state == AppState::Binding) {
        drawSdlText(renderer_, hint_rect, "Go to xiaozhi.me to bind", {{240, 240, 240, 255}, 17.0f, false, false, false});
        drawSdlText(renderer_, code_rect, std::string("Code: ") + extractCode(source_text), {{255, 244, 180, 255}, 30.0f, true, false, false});
    } else if (state == AppState::Idle) {
        drawSdlText(renderer_, hint_rect, "SPACE / ENTER TO WAKE", {{240, 240, 240, 255}, 17.0f, false, false, false});
    }

    SDL_Rect status = {kPanelX, kStatusY, kPanelW, kStatusH};
    SDL_SetRenderDrawColor(renderer_, 235, 235, 235, 255);
    SDL_RenderFillRect(renderer_, &status);

    drawSdlText(renderer_, {kPanelX + kPanelPad, kStatusY, kPanelW - (kPanelPad * 2), kStatusH}, display_text, {{15, 15, 15, 255}, 16.0f, false, false, false});

    SDL_RenderPresent(renderer_);
    saveSnapshotIfEnabled();

    std::string title = std::string("cardputer-xiaozhi | ") + stateName(state) + " | " + source_text;
    SDL_SetWindowTitle(window_, title.c_str());
}

std::string UiSdl::marqueeText(const std::string& text) {
    constexpr size_t kVisibleGlyphs = 26;
    constexpr Uint32 kStepMs = 350;
    const std::vector<std::string> glyphs = splitUtf8Glyphs(text);
    if (glyphs.size() <= kVisibleGlyphs) {
        marquee_source_text_.clear();
        marquee_start_ticks_ = 0;
        return text;
    }

    if (text != marquee_source_text_) {
        marquee_source_text_ = text;
        marquee_start_ticks_ = SDL_GetTicks();
    }

    const size_t max_offset = glyphs.size() - kVisibleGlyphs;
    const Uint32 elapsed = SDL_GetTicks() - marquee_start_ticks_;
    const size_t offset = std::min(max_offset, static_cast<size_t>(elapsed / kStepMs));
    std::string out;
    for (size_t i = 0; i < kVisibleGlyphs && offset + i < glyphs.size(); ++i) {
        out += glyphs[offset + i];
    }
    return out;
}

void UiSdl::saveSnapshotIfEnabled() {
    if (snapshot_path_.empty() || renderer_ == nullptr) {
        return;
    }

    int width = 0;
    int height = 0;
    if (SDL_GetRendererOutputSize(renderer_, &width, &height) != 0 || width <= 0 || height <= 0) {
        return;
    }

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    if (surface == nullptr) {
        return;
    }

    if (SDL_RenderReadPixels(renderer_, nullptr, SDL_PIXELFORMAT_ARGB8888, surface->pixels, surface->pitch) == 0) {
        SDL_SaveBMP(surface, snapshot_path_.c_str());
    }
    SDL_FreeSurface(surface);
}

const char* UiSdl::stateName(AppState state) const {
    switch (state) {
        case AppState::Binding:
            return "BINDING";
        case AppState::Idle:
            return "IDLE";
        case AppState::Listening:
            return "LISTENING...";
        case AppState::Thinking:
            return "THINKING...";
        case AppState::Speaking:
            return "SPEAKING...";
        case AppState::Error:
            return "ERROR";
    }
    return "UNKNOWN";
}

const char* UiSdl::stateEmoji(AppState state) const {
    switch (state) {
        case AppState::Binding:
            return "🔗";
        case AppState::Idle:
            return "😄";
        case AppState::Listening:
            return "🎙️";
        case AppState::Thinking:
            return "🤔";
        case AppState::Speaking:
            return "🔊";
        case AppState::Error:
            return "❌";
    }
    return "❓";
}

}  // namespace xiaozhi
