#include "hal.hpp"

#include <iostream>

namespace xiaozhi {

bool HalStub::init() {
    std::cout << "[hal] init stub" << std::endl;
    return true;
}

void HalStub::poll() {
    // TODO: replace with evdev/gpio event integration on real hardware.
}

void HalStub::onButtonPressed(std::function<void()> cb) {
    press_cb_ = std::move(cb);
}

void HalStub::onButtonReleased(std::function<void()> cb) {
    release_cb_ = std::move(cb);
}

}  // namespace xiaozhi
