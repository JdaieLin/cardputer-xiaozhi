#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <thread>

#include "application.hpp"
#include "audio_pipeline_sdl.hpp"
#include "display_bridge.hpp"
#include "hal_evdev.hpp"

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

    std::cout << "[main] xiaozhi device starting" << std::endl;

    auto config = xiaozhi::loadConfig();
    std::cout << "[main] ota_url=" << config.ota_url << std::endl;
    std::cout << "[main] ws_url=" << config.ws_url << std::endl;

    xiaozhi::Application app(
        config,
        std::make_unique<xiaozhi::HalEvdev>(),
        std::make_unique<xiaozhi::DisplayBridge>(),
        std::make_unique<xiaozhi::WsClientBridge>(),
        std::make_unique<xiaozhi::AudioPipelineSdl>());

    if (!app.start()) {
        std::cerr << "[main] Failed to start device application" << std::endl;
        return 1;
    }

    std::cout << "[main] application started, entering main loop" << std::endl;

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
