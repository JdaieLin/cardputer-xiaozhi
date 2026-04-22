#pragma once

#include <functional>
#include <string>
#include <vector>

#include "hal.hpp"
#include <SDL.h>

namespace xiaozhi {

class HalSdl final : public Hal {
public:
    HalSdl() = default;
    ~HalSdl() override;

    bool init() override;
    void poll() override;
    bool shouldQuit() const override;
    void onButtonPressed(std::function<void()> cb) override;
    void onButtonReleased(std::function<void()> cb) override;

private:
    bool isWakeKey(const SDL_KeyboardEvent& event) const;
    void loadWakeKeys();

    std::function<void()> press_cb_;
    std::function<void()> release_cb_;
    bool should_quit_ = false;
    SDL_Keycode active_wake_key_ = SDLK_UNKNOWN;
    std::vector<SDL_Keycode> wake_keys_;
};

}  // namespace xiaozhi
