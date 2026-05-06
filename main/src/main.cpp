#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <thread>

#include "application.hpp"

namespace {
volatile sig_atomic_t g_running = 1;

void signalHandler(int signum) {
    (void)signum;
    g_running = 0;
}
}  // namespace

int main() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    auto config = xiaozhi::loadConfig();

    xiaozhi::Application app(
        config,
        std::make_unique<xiaozhi::HalStub>(),
        std::make_unique<xiaozhi::UiStub>(),
        std::make_unique<xiaozhi::WsClientStub>(),
        std::make_unique<xiaozhi::AudioPipelineStub>());

    if (!app.start()) {
        std::cerr << "Failed to start application" << std::endl;
        return 1;
    }

    while (g_running && app.isRunning()) {
        app.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    app.stop();
    return 0;
}
