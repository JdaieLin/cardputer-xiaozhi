#include "application.hpp"

#include <chrono>
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
    if (!hal_->init()) {
        setState(AppState::Error, "hal init failed");
        return false;
    }

    if (!ui_->init()) {
        std::cerr << "[app] ui init failed, continuing headless" << std::endl;
    }

    hal_->onButtonPressed([this]() { onButtonPressed(); });
    hal_->onButtonReleased([this]() { onButtonReleased(); });
    last_ui_refresh_ = std::chrono::steady_clock::now();

    ws_->setOnServerText([this](const std::string& msg) {
        if (state_ == AppState::Listening && audio_->isCapturing()) {
            audio_->stopCapture();
            setState(AppState::Thinking, "server vad stop waiting response", true);
        } else {
            setState(AppState::Thinking, "server text", true);
        }
        updateDisplayMessage("🗣️ " + msg);
    });
    ws_->setOnEmotion([this](const std::string& emoji) {
        if (emoji.empty()) {
            return;
        }
        current_emoji_ = emoji;
        renderUi();
    });
    ws_->setOnTtsText([this](const std::string& msg) {
        if (msg.empty()) {
            return;
        }
        tts_text_buffer_ += msg;
        updateDisplayMessage(tts_text_buffer_);
    });
    ws_->setOnListenStop([this]() {
        if (audio_->isCapturing()) {
            audio_->stopCapture();
            setState(AppState::Thinking, "server vad stop waiting response", true);
        } else if (state_ == AppState::Idle) {
            keep_listening_ = false;
        }
    });
    ws_->setOnTtsStart([this]() {
        if (audio_->isCapturing()) {
            audio_->stopCapture();
        }
        setState(AppState::Speaking, "tts start", true);
    });
    ws_->setOnTtsPcm([this](const std::vector<int16_t>& pcm) {
        audio_->playPcmFrame(pcm);
    });
    ws_->setOnTtsStop([this]() {
        if (keep_listening_ && connected_) {
            startListening(true);
            return;
        }
        setState(AppState::Idle, "tts stop", true);
    });
    ws_->setOnGoodbye([this]() {
        keep_listening_ = false;
        setState(AppState::Idle, "server ended conversation", true);
    });
    ws_->setOnDisconnected([this]() { handleBackendDisconnected(); });

    running_ = true;
    tick_count_ = 0;
    bind_poll_tick_ = 0;
    activated_ = false;
    connected_ = false;
    connect_retry_count_ = 0;
    keep_listening_ = false;
    listen_after_connect_ = false;
    tts_text_buffer_.clear();
    status_text_.clear();
    display_text_.clear();
    current_emoji_.clear();

    if (!audio_->init()) {
        std::cerr << "[app] audio init failed, running without sound I/O" << std::endl;
        setState(AppState::Error, "audio init failed, no sound I/O");
        return true;
    }

    const OtaStatus ota_status = ota_.check();
    cfg_.device_id = ota_.deviceId();
    cfg_.client_id = ota_.clientId();
    if (!ota_status.ok) {
        setState(AppState::Error, "ota failed " + ota_status.error);
        return true;
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
    keep_listening_ = false;
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
        connect_retry_count_++;
        if (connect_retry_count_ < 120) {
            return;
        }
        connect_retry_count_ = 0;
        if (!connectBackend()) {
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

    const auto now = std::chrono::steady_clock::now();
    if (now - last_ui_refresh_ >= std::chrono::milliseconds(150)) {
        renderUi();
        last_ui_refresh_ = now;
    }
}

bool Application::isRunning() const {
    return running_;
}

void Application::setState(AppState state, const std::string& text, bool preserve_display) {
    state_ = state;
    status_text_ = text;
    if (!preserve_display || display_text_.empty()) {
        display_text_ = text;
    }
    std::cout << "[state] " << static_cast<int>(state_) << " | " << text << std::endl;
    renderUi();
}

void Application::updateDisplayMessage(const std::string& text) {
    display_text_ = text;
    renderUi();
}

void Application::renderUi() {
    ui_->renderState(state_, display_text_.empty() ? status_text_ : display_text_, current_emoji_);
}

void Application::startListening(bool preserve_display) {
    std::cout << "[wake] startListening connected=" << connected_
              << " capturing=" << audio_->isCapturing()
              << " preserve_display=" << preserve_display << std::endl;
    if (connected_ && !audio_->isCapturing()) {
        tts_text_buffer_.clear();
        ws_->sendListenStart();
        audio_->startCapture();
        setState(AppState::Listening, "listening server vad", preserve_display);
    }
}

void Application::handleBackendDisconnected() {
    if (!connected_) {
        return;
    }
    connected_ = false;
    keep_listening_ = false;
    listen_after_connect_ = false;
    audio_->stopCapture();
    current_emoji_.clear();
    setState(AppState::Idle, "Ready", false);
}

void Application::onButtonPressed() {
    std::cout << "[wake] onButtonPressed running=" << running_
              << " activated=" << activated_
              << " connected=" << connected_
              << " state=" << static_cast<int>(state_)
              << " capturing=" << audio_->isCapturing() << std::endl;
    if (!running_) {
        return;
    }

    if (!activated_) {
        setState(AppState::Binding, "code " + binding_code_ + " waiting bind");
        return;
    }

    if (!connected_) {
        listen_after_connect_ = true;
        keep_listening_ = true;
        setState(AppState::Thinking, "connecting", true);
        return;
    }

    if (state_ == AppState::Speaking) {
        keep_listening_ = false;
        ws_->sendAbort();
        setState(AppState::Thinking, "user wakeup interrupt", true);
    }

    keep_listening_ = true;
    startListening(true);
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
        current_emoji_.clear();
        if (listen_after_connect_) {
            listen_after_connect_ = false;
            keep_listening_ = true;
            startListening();
            return true;
        }
        keep_listening_ = false;
        setState(AppState::Idle, "Ready");
        return true;
    }
    setState(AppState::Error, "websocket connect failed");
    return false;
}

}  // namespace xiaozhi
