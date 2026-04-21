#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <thread>

#include "application.hpp"
#include "audio_pipeline_sdl.hpp"
#include "hal_sdl.hpp"
#include "ui_sdl.hpp"

namespace {
volatile sig_atomic_t g_running = 1;
volatile sig_atomic_t g_last_signal = 0;

void signalHandler(int signum) {
    g_last_signal = signum;
    g_running = 0;
}
}  // namespace

int main() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    auto config = xiaozhi::loadConfig();

    xiaozhi::Application app(
        config,
        std::make_unique<xiaozhi::HalSdl>(),
        std::make_unique<xiaozhi::UiSdl>(),
        std::make_unique<xiaozhi::WsClientBridge>(),
        std::make_unique<xiaozhi::AudioPipelineSdl>());

    if (!app.start()) {
        std::cerr << "Failed to start simulator application" << std::endl;
        return 1;
    }

    while (g_running && app.isRunning()) {
        app.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    if (g_last_signal != 0) {
        std::cout << "[exit] signal=" << g_last_signal << std::endl;
    }

    app.stop();
    return 0;
}
