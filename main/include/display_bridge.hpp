#pragma once

#include <string>

#include "types.hpp"
#include "ui.hpp"

namespace xiaozhi {

class DisplayBridge final : public Ui {
public:
    DisplayBridge();
    ~DisplayBridge() override;

    bool init() override;
    void setTerminalText(const std::string& text) override;
    void setAudioSamples(const std::vector<int16_t>& pcm, bool assistant) override;
    void toggleDisplayMode() override;
    void setCommandActive(bool active) override;
    void setCameraFrame(const std::string& jpeg_base64) override;
    void renderState(AppState state, const std::string& text, const std::string& emoji) override;

    void disconnect();

private:
    void sendJsonLine(const std::string& json_line);

    int child_stdin_fd_ = -1;
    int child_stdout_fd_ = -1;
    int child_pid_ = -1;
    bool connected_ = false;
    std::string terminal_text_;
};

}  // namespace xiaozhi
