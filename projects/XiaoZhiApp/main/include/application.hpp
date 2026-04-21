#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "audio_pipeline.hpp"
#include "config.hpp"
#include "hal.hpp"
#include "ota_client.hpp"
#include "types.hpp"
#include "ui.hpp"
#include "ws_client.hpp"

namespace xiaozhi {

class Application {
public:
    Application(AppConfig cfg,
                std::unique_ptr<Hal> hal,
                std::unique_ptr<Ui> ui,
                std::unique_ptr<WsClient> ws,
                std::unique_ptr<AudioPipeline> audio);

    bool start();
    void stop();
    void tick();
    bool isRunning() const;

private:
    void setState(AppState state, const std::string& text);
    void onButtonPressed();
    void onButtonReleased();
    void tickBindingFlow();
    bool connectBackend();

    AppConfig cfg_;
    AppState state_ = AppState::Idle;
    bool running_ = false;
    bool activated_ = false;
    bool connected_ = false;
    int tick_count_ = 0;
    int bind_poll_tick_ = 0;
    std::string binding_code_;
    std::string binding_challenge_;

    std::unique_ptr<Hal> hal_;
    std::unique_ptr<Ui> ui_;
    std::unique_ptr<WsClient> ws_;
    std::unique_ptr<AudioPipeline> audio_;
    OtaClient ota_;
};

}  // namespace xiaozhi
