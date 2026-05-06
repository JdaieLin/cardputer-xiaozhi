#pragma once

#include <string>

#include "config.hpp"

namespace xiaozhi {

struct OtaStatus {
    bool ok = false;
    bool needs_binding = false;
    bool paired = false;
    std::string activation_code;
    std::string activation_challenge;
    std::string ws_url;
    std::string ws_token;
    std::string error;
};

enum class ActivateResult {
    Ok,
    Pending,
    Error,
};

class OtaClient {
public:
    explicit OtaClient(AppConfig cfg);

    OtaStatus check();
    ActivateResult activate(const std::string& challenge);
    const std::string& deviceId() const;
    const std::string& clientId() const;

private:
    std::string makeRequestBody() const;
    std::string postJson(const std::string& url, const std::string& body) const;
    int postJsonCode(const std::string& url, const std::string& body) const;
    std::string shellEscape(const std::string& value) const;
    std::string extract(const std::string& text, const std::string& regex_expr) const;
    std::string pythonDigest(const std::string& algo, const std::string& text) const;
    std::string pythonHmacSha256(const std::string& key, const std::string& text) const;

    AppConfig cfg_;
    std::string serial_number_;
    std::string hmac_key_;
};

}  // namespace xiaozhi
