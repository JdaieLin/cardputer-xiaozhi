#pragma once

#include <functional>

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
    std::function<void()> press_cb_;
    std::function<void()> release_cb_;
    bool should_quit_ = false;
};

}  // namespace xiaozhi
