#pragma once

#include <string>

namespace xiaozhi {

struct AppConfig {
    std::string ota_url = "https://api.tenclass.net/xiaozhi/ota/";
    std::string ws_url = "wss://api.tenclass.net/xiaozhi/v1/";
    std::string ws_token;
    std::string device_id;
    std::string client_id;
    int input_rate = 16000;
    int output_rate = 24000;
    int channels = 1;
};

AppConfig loadConfig();

}  // namespace xiaozhi
