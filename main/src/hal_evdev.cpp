#include "hal_evdev.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/input.h>
#include <unistd.h>

namespace xiaozhi {

HalEvdev::HalEvdev() = default;

HalEvdev::~HalEvdev() {
    if (fd_ >= 0) {
        close(fd_);
    }
}

bool HalEvdev::init() {
    const char* device = std::getenv("APPLAUNCH_LINUX_KEYBOARD_DEVICE");
    if (device == nullptr || *device == '\0') {
        device = std::getenv("XIAOZHI_KEYBOARD_DEVICE");
    }
    if (device == nullptr || *device == '\0') {
        device = "/dev/input/by-path/platform-3f804000.i2c-event";
    }

    fd_ = open(device, O_RDONLY | O_NONBLOCK);
    if (fd_ < 0) {
        std::cerr << "[hal-evdev] failed to open " << device << ": "
                  << std::strerror(errno) << std::endl;
        return false;
    }

    int flags = fcntl(fd_, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
    }

    std::cout << "[hal-evdev] initialized on " << device << std::endl;
    return true;
}

void HalEvdev::poll() {
    if (fd_ < 0) {
        return;
    }

    struct input_event ev;
    while (true) {
        const ssize_t n = read(fd_, &ev, sizeof(ev));
        if (n < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                std::cerr << "[hal-evdev] read error: " << std::strerror(errno) << std::endl;
                should_quit_ = true;
            }
            break;
        }
        if (n == 0) {
            should_quit_ = true;
            break;
        }
        if (static_cast<size_t>(n) < sizeof(ev)) {
            continue;
        }

        if (ev.type != EV_KEY) {
            continue;
        }

        if (ev.code == KEY_SPACE) {
            if (ev.value == 1 && !space_pressed_) {
                space_pressed_ = true;
                std::cout << "[hal-evdev] SPACE press" << std::endl;
                if (press_cb_) press_cb_();
            } else if (ev.value == 0 && space_pressed_) {
                space_pressed_ = false;
                std::cout << "[hal-evdev] SPACE release" << std::endl;
                if (release_cb_) release_cb_();
            }
        } else if (ev.code == KEY_ENTER) {
            if (ev.value == 1 && !enter_pressed_) {
                enter_pressed_ = true;
                std::cout << "[hal-evdev] ENTER press" << std::endl;
                if (press_cb_) press_cb_();
            } else if (ev.value == 0 && enter_pressed_) {
                enter_pressed_ = false;
                std::cout << "[hal-evdev] ENTER release" << std::endl;
                if (release_cb_) release_cb_();
            }
        } else if (ev.code == KEY_HOME || ev.code == KEY_ESC) {
            if (ev.value == 1) {
                std::cout << "[hal-evdev] HOME/ESC press -> request quit" << std::endl;
                should_quit_ = true;
            }
        }
    }
}

bool HalEvdev::shouldQuit() const {
    return should_quit_;
}

void HalEvdev::onButtonPressed(std::function<void()> cb) {
    press_cb_ = std::move(cb);
}

void HalEvdev::onButtonReleased(std::function<void()> cb) {
    release_cb_ = std::move(cb);
}

}  // namespace xiaozhi
