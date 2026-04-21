#include "hal_sdl.hpp"

#include <iostream>

namespace xiaozhi {

HalSdl::~HalSdl() {
    SDL_Quit();
}

bool HalSdl::init() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO) != 0) {
        std::cerr << "[hal-sdl] SDL_Init failed: " << SDL_GetError() << std::endl;
        return false;
    }
    std::cout << "[hal-sdl] initialized" << std::endl;
    return true;
}

void HalSdl::poll() {
    SDL_Event event;
    while (SDL_PollEvent(&event) == 1) {
        if (event.type == SDL_QUIT) {
            should_quit_ = true;
            continue;
        }

        if (event.type == SDL_KEYDOWN && event.key.repeat == 0 && event.key.keysym.sym == SDLK_SPACE) {
            if (press_cb_) {
                press_cb_();
            }
            continue;
        }

        if (event.type == SDL_KEYUP && event.key.repeat == 0 && event.key.keysym.sym == SDLK_SPACE) {
            if (release_cb_) {
                release_cb_();
            }
            continue;
        }
    }
}

bool HalSdl::shouldQuit() const {
    return should_quit_;
}

void HalSdl::onButtonPressed(std::function<void()> cb) {
    press_cb_ = std::move(cb);
}

void HalSdl::onButtonReleased(std::function<void()> cb) {
    release_cb_ = std::move(cb);
}

}  // namespace xiaozhi
