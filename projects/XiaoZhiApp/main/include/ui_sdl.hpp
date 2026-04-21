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
    const char* stateEmoji(AppState state) const;
    void saveSnapshotIfEnabled();
    std::string marqueeText(const std::string& text);

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    std::string snapshot_path_;
    std::string force_text_;
    std::string marquee_source_text_;
    Uint32 marquee_start_ticks_ = 0;
};

}  // namespace xiaozhi
