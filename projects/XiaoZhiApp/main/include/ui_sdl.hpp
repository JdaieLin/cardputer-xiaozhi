#pragma once

#include <string>

#include "types.hpp"
#include "ui.hpp"
#include <SDL.h>

namespace xiaozhi {

class UiSdl final : public Ui {
public:
    UiSdl() = default;
    ~UiSdl() override;

    bool init() override;
    void renderState(AppState state, const std::string& text) override;

private:
    const char* stateName(AppState state) const;

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
};

}  // namespace xiaozhi
