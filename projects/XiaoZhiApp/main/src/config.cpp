#include "config.hpp"

#include <cstdlib>

namespace xiaozhi {
namespace {

int envInt(const char* key, int fallback) {
    const char* value = std::getenv(key);
    if (value == nullptr) {
        return fallback;
    }
    return std::atoi(value);
}

std::string envString(const char* key, const std::string& fallback) {
    const char* value = std::getenv(key);
    if (value == nullptr) {
        return fallback;
    }
    return std::string(value);
}

}  // namespace

AppConfig loadConfig() {
    AppConfig cfg;
    cfg.ota_url = envString("XIAOZHI_OTA_URL", cfg.ota_url);
    cfg.ws_url = envString("XIAOZHI_WS_URL", cfg.ws_url);
    cfg.ws_token = envString("XIAOZHI_WS_TOKEN", cfg.ws_token);
    cfg.device_id = envString("DEVICE_ID", cfg.device_id);
    cfg.client_id = envString("CLIENT_ID", cfg.client_id);
    cfg.input_rate = envInt("AUDIO_INPUT_RATE", cfg.input_rate);
    cfg.output_rate = envInt("AUDIO_OUTPUT_RATE", cfg.output_rate);
    cfg.channels = envInt("AUDIO_CHANNELS", cfg.channels);
    return cfg;
}

}  // namespace xiaozhi
