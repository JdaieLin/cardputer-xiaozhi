#include "ui.hpp"

#include <iostream>

namespace xiaozhi {
namespace {

const char* stateName(AppState state) {
    switch (state) {
        case AppState::Idle:
            return "IDLE";
        case AppState::Listening:
            return "LISTENING";
        case AppState::Thinking:
            return "THINKING";
        case AppState::Speaking:
            return "SPEAKING";
        case AppState::Error:
            return "ERROR";
    }
    return "UNKNOWN";
}

}  // namespace

bool UiStub::init() {
    std::cout << "[ui] init stub" << std::endl;
    return true;
}

void UiStub::renderState(AppState state, const std::string& text) {
    std::cout << "[ui] " << stateName(state) << " | " << text << std::endl;
}

}  // namespace xiaozhi
