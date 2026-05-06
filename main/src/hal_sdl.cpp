#include "hal_sdl.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>

namespace xiaozhi {
namespace {

bool g_sdl_video_ok = false;

std::vector<SDL_Keycode> defaultWakeKeys() {
    return {SDLK_SPACE, SDLK_RETURN, SDLK_KP_ENTER};
}

std::vector<SDL_Keycode> parseWakeKeys(const char* env_value) {
    if (env_value == nullptr || *env_value == '\0') {
        return {};
    }

    std::vector<SDL_Keycode> keys;
    std::stringstream stream(env_value);
    std::string token;
    while (std::getline(stream, token, ',')) {
        const size_t start = token.find_first_not_of(" \t");
        if (start == std::string::npos) {
            continue;
        }
        const size_t end = token.find_last_not_of(" \t");
        const std::string name = token.substr(start, end - start + 1);
        const SDL_Keycode key = SDL_GetKeyFromName(name.c_str());
        if (key != SDLK_UNKNOWN) {
            keys.push_back(key);
        }
    }
    return keys;
}

std::string wakeKeyNames(const std::vector<SDL_Keycode>& keys) {
    std::string out;
    for (size_t i = 0; i < keys.size(); ++i) {
        const char* name = SDL_GetKeyName(keys[i]);
        if (name == nullptr || *name == '\0') {
            continue;
        }
        if (!out.empty()) {
            out += ", ";
        }
        out += name;
    }
    return out;
}

}  // namespace

bool sdlVideoOk() {
    return g_sdl_video_ok;
}

HalSdl::~HalSdl() {
    SDL_Quit();
}

bool HalSdl::init() {
    Uint32 flags = SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO;
    int rc = SDL_Init(flags);

    if (rc == 0) {
        g_sdl_video_ok = true;
        loadWakeKeys();
        std::cout << "[hal-sdl] initialized (video+events+audio)" << std::endl;
        return true;
    }

    std::cerr << "[hal-sdl] SDL_Init(video+events+audio) failed: " << SDL_GetError() << std::endl;
    std::cerr << "[hal-sdl] retrying without video subsystem" << std::endl;

    SDL_Quit();
    rc = SDL_Init(SDL_INIT_EVENTS | SDL_INIT_AUDIO);
    if (rc != 0) {
        std::cerr << "[hal-sdl] SDL_Init(events+audio) failed: " << SDL_GetError() << std::endl;
        return false;
    }

    g_sdl_video_ok = false;
    loadWakeKeys();
    std::cout << "[hal-sdl] initialized (events+audio only, headless)" << std::endl;
    return true;
}

void HalSdl::poll() {
    SDL_Event event;
    while (SDL_PollEvent(&event) == 1) {
        if (event.type == SDL_QUIT) {
            should_quit_ = true;
            continue;
        }

        if (event.type == SDL_KEYDOWN && event.key.repeat == 0 && isWakeKey(event.key)) {
            active_wake_key_ = event.key.keysym.sym;
            if (press_cb_) {
                press_cb_();
            }
            continue;
        }

        if (event.type == SDL_KEYUP && event.key.repeat == 0 && event.key.keysym.sym == active_wake_key_) {
            active_wake_key_ = SDLK_UNKNOWN;
            if (release_cb_) {
                release_cb_();
            }
            continue;
        }
    }
}

bool HalSdl::isWakeKey(const SDL_KeyboardEvent& event) const {
    for (const SDL_Keycode key : wake_keys_) {
        if (event.keysym.sym == key) {
            return true;
        }
    }
    return false;
}

bool HalSdl::shouldQuit() const {
    return should_quit_;
}

void HalSdl::loadWakeKeys() {
    wake_keys_ = parseWakeKeys(std::getenv("CARDPUTER_WAKE_KEYS"));
    if (wake_keys_.empty()) {
        wake_keys_ = defaultWakeKeys();
    }

    std::cout << "[hal-sdl] wake keys: " << wakeKeyNames(wake_keys_) << std::endl;
}

void HalSdl::onButtonPressed(std::function<void()> cb) {
    press_cb_ = std::move(cb);
}

void HalSdl::onButtonReleased(std::function<void()> cb) {
    release_cb_ = std::move(cb);
}

}  // namespace xiaozhi
