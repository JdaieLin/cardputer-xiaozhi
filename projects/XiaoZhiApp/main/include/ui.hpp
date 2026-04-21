#pragma once

#include <string>

#include "types.hpp"

namespace xiaozhi {

class Ui {
public:
    virtual ~Ui() = default;
    virtual bool init() = 0;
    virtual void renderState(AppState state, const std::string& text, const std::string& emoji) = 0;
};

class UiStub final : public Ui {
public:
    bool init() override;
    void renderState(AppState state, const std::string& text, const std::string& emoji) override;
};

}  // namespace xiaozhi
