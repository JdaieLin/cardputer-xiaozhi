#pragma once

#include <functional>
#include <string>

#include "hal.hpp"

namespace xiaozhi {

class HalEvdev final : public Hal {
public:
    HalEvdev();
    ~HalEvdev() override;

    bool init() override;
    void poll() override;
    bool shouldQuit() const override;
    void onButtonPressed(std::function<void()> cb) override;
    void onButtonReleased(std::function<void()> cb) override;

private:
    std::function<void()> press_cb_;
    std::function<void()> release_cb_;
    int fd_ = -1;
    bool should_quit_ = false;
    bool space_pressed_ = false;
    bool enter_pressed_ = false;
};

}  // namespace xiaozhi
