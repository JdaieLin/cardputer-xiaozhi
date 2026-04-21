#pragma once

#include <functional>

namespace xiaozhi {

class Hal {
public:
    virtual ~Hal() = default;
    virtual bool init() = 0;
    virtual void poll() = 0;
    virtual bool shouldQuit() const { return false; }
    virtual void onButtonPressed(std::function<void()> cb) = 0;
    virtual void onButtonReleased(std::function<void()> cb) = 0;
};

class HalStub final : public Hal {
public:
    bool init() override;
    void poll() override;
    void onButtonPressed(std::function<void()> cb) override;
    void onButtonReleased(std::function<void()> cb) override;

private:
    std::function<void()> press_cb_;
    std::function<void()> release_cb_;
};

}  // namespace xiaozhi
