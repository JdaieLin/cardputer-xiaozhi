#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "types.hpp"

namespace xiaozhi {

class Ui {
public:
    virtual ~Ui() = default;
    virtual bool init() = 0;
    virtual void setTerminalText(const std::string& text) { (void)text; }
    virtual void setAudioSamples(const std::vector<int16_t>& pcm, bool assistant) {
        (void)pcm;
        (void)assistant;
    }
    virtual void toggleDisplayMode() {}
    virtual void setCommandActive(bool active) { (void)active; }
    virtual void renderState(AppState state, const std::string& text, const std::string& emoji) = 0;
};

class UiStub final : public Ui {
public:
    bool init() override;
    void renderState(AppState state, const std::string& text, const std::string& emoji) override;
};

}  // namespace xiaozhi
