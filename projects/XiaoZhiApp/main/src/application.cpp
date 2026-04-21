#include "application.hpp"

#include <iostream>
#include <utility>

namespace xiaozhi {

Application::Application(AppConfig cfg,
                         std::unique_ptr<Hal> hal,
                         std::unique_ptr<Ui> ui,
                         std::unique_ptr<WsClient> ws,
                         std::unique_ptr<AudioPipeline> audio)
    : cfg_(std::move(cfg)),
      hal_(std::move(hal)),
      ui_(std::move(ui)),
      ws_(std::move(ws)),
      audio_(std::move(audio)),
      ota_(cfg_) {}

bool Application::start() {
    if (!hal_->init() || !ui_->init() || !audio_->init()) {
        setState(AppState::Error, "failed to initialize subsystem");
        return false;
    }

    hal_->onButtonPressed([this]() { onButtonPressed(); });
    hal_->onButtonReleased([this]() { onButtonReleased(); });

    ws_->setOnServerText([this](const std::string& msg) {
        setState(AppState::Thinking, msg);
    });
    ws_->setOnListenStop([this]() {
        if (audio_->isCapturing()) {
            audio_->stopCapture();
            setState(AppState::Thinking, "server vad stop waiting response");
        }
    });
    ws_->setOnTtsStart([this]() {
        if (audio_->isCapturing()) {
            audio_->stopCapture();
        }
        setState(AppState::Speaking, "tts start");
    });
    ws_->setOnTtsPcm([this](const std::vector<int16_t>& pcm) {
        audio_->playPcmFrame(pcm);
    });
    ws_->setOnTtsStop([this]() { setState(AppState::Idle, "tts stop"); });

    running_ = true;
    tick_count_ = 0;
    activated_ = false;
    connected_ = false;

    const OtaStatus ota_status = ota_.check();
    cfg_.device_id = ota_.deviceId();
    cfg_.client_id = ota_.clientId();
    if (!ota_status.ok) {
        setState(AppState::Error, "ota failed " + ota_status.error);
        return false;
    }

    if (ota_status.paired) {
        activated_ = true;
        if (!ota_status.ws_url.empty()) {
            cfg_.ws_url = ota_status.ws_url;
        }
        if (!ota_status.ws_token.empty()) {
            cfg_.ws_token = ota_status.ws_token;
        }
        setState(AppState::Thinking, "paired connecting");
        return true;
    }

    binding_code_ = ota_status.activation_code;
    if (binding_code_.empty()) {
        binding_code_ = "------";
    }
    setState(AppState::Binding, "code " + binding_code_ + " open app to bind");
    return true;
}

void Application::stop() {
    if (!running_) {
        return;
    }
    audio_->stopCapture();
    ws_->disconnect();
    running_ = false;
    setState(AppState::Idle, "stopped");
}

void Application::tick() {
    if (!running_) {
        return;
    }

    ++tick_count_;
    hal_->poll();
    if (hal_->shouldQuit()) {
        std::cout << "[exit] SDL quit event" << std::endl;
        stop();
        return;
    }

    if (!activated_) {
        tickBindingFlow();
        return;
    }

    if (!connected_) {
        if (!connectBackend()) {
            stop();
            return;
        }
    }

    ws_->poll();

    if (audio_->isCapturing()) {
        auto frame = audio_->readPcmFrame();
        if (!frame.empty()) {
            ws_->sendAudioFrame(frame);
        }
    }
}

bool Application::isRunning() const {
    return running_;
}

void Application::setState(AppState state, const std::string& text) {
    state_ = state;
    std::cout << "[state] " << static_cast<int>(state_) << " | " << text << std::endl;
    ui_->renderState(state_, text);
}

void Application::onButtonPressed() {
    if (!running_) {
        return;
    }

    if (!activated_) {
        setState(AppState::Binding, "code " + binding_code_ + " waiting bind");
        return;
    }

    if (state_ == AppState::Speaking) {
        ws_->sendAbort();
        setState(AppState::Thinking, "user wakeup interrupt");
    }

    if (!audio_->isCapturing()) {
        ws_->sendListenStart();
        audio_->startCapture();
        setState(AppState::Listening, "listening server vad");
    }
}

void Application::onButtonReleased() {
    // Wake key mode: release should not stop listening.
}

void Application::tickBindingFlow() {
    ++bind_poll_tick_;
    if (bind_poll_tick_ < 120) {
        return;
    }
    bind_poll_tick_ = 0;

    const OtaStatus ota_status = ota_.check();
    cfg_.device_id = ota_.deviceId();
    cfg_.client_id = ota_.clientId();
    if (!ota_status.ok) {
        setState(AppState::Binding, "code " + binding_code_ + " waiting bind");
        return;
    }

    if (ota_status.paired) {
        activated_ = true;
        if (!ota_status.ws_url.empty()) {
            cfg_.ws_url = ota_status.ws_url;
        }
        if (!ota_status.ws_token.empty()) {
            cfg_.ws_token = ota_status.ws_token;
        }
        setState(AppState::Thinking, "binding complete connecting");
        return;
    }

    if (!ota_status.activation_code.empty()) {
        binding_code_ = ota_status.activation_code;
    }

    if (!ota_status.activation_challenge.empty()) {
        const ActivateResult ar = ota_.activate(ota_status.activation_challenge);
        if (ar == ActivateResult::Ok) {
            setState(AppState::Binding, "code " + binding_code_ + " bind accepted syncing");
            return;
        }
        if (ar == ActivateResult::Error) {
            setState(AppState::Binding, "code " + binding_code_ + " activate retry");
            return;
        }
    }

    setState(AppState::Binding, "code " + binding_code_ + " waiting bind");
}

bool Application::connectBackend() {
    if (ws_->connect(cfg_.ws_url, cfg_.ws_token, cfg_.device_id, cfg_.client_id)) {
        connected_ = true;
        setState(AppState::Idle, "ready");
        return true;
    }
    setState(AppState::Error, "websocket connect failed");
    return false;
}

}  // namespace xiaozhi
