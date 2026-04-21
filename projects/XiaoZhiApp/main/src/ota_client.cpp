#include "ota_client.hpp"

#include <mach-o/dyld.h>

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <regex>
#include <sstream>

namespace xiaozhi {
namespace {

std::string randomHex(int n) {
    static const char* kHex = "0123456789abcdef";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(0, 15);
    std::string out;
    out.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        out.push_back(kHex[dis(gen)]);
    }
    return out;
}

std::string randomUuidV4() {
    std::string h = randomHex(32);
    h[12] = '4';
    const char variants[] = {'8', '9', 'a', 'b'};
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(0, 3);
    h[16] = variants[dis(gen)];

    return h.substr(0, 8) + "-" + h.substr(8, 4) + "-" + h.substr(12, 4) + "-" +
           h.substr(16, 4) + "-" + h.substr(20, 12);
}

std::string randomMac() {
    const std::string h = randomHex(12);
    std::ostringstream oss;
    for (int i = 0; i < 12; i += 2) {
        if (i > 0) {
            oss << ':';
        }
        oss << h.substr(static_cast<size_t>(i), 2);
    }
    return oss.str();
}

std::string trim(const std::string& s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b])) != 0) {
        ++b;
    }
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])) != 0) {
        --e;
    }
    return s.substr(b, e - b);
}

std::filesystem::path executablePath() {
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(static_cast<size_t>(size), '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return {};
    }
    return std::filesystem::weakly_canonical(std::filesystem::path(buffer.c_str()));
}

std::string identityFilePath() {
    const std::filesystem::path exe = executablePath();
    if (!exe.empty()) {
        const std::filesystem::path project_dir = exe.parent_path().parent_path();
        const std::filesystem::path repo_root = project_dir.parent_path().parent_path();
        if (std::filesystem::exists(repo_root / "build.sh")) {
            return (repo_root / "sim_identity.env").string();
        }
    }
    return "sim_identity.env";
}

}  // namespace

OtaClient::OtaClient(AppConfig cfg) : cfg_(std::move(cfg)) {
    const std::string identity_file = identityFilePath();

    if (cfg_.device_id.empty() || cfg_.client_id.empty()) {
        std::ifstream in(identity_file);
        if (in.good()) {
            std::string line;
            while (std::getline(in, line)) {
                if (line.rfind("DEVICE_ID=", 0) == 0 && cfg_.device_id.empty()) {
                    cfg_.device_id = trim(line.substr(10));
                } else if (line.rfind("CLIENT_ID=", 0) == 0 && cfg_.client_id.empty()) {
                    cfg_.client_id = trim(line.substr(10));
                }
            }
        }
    }

    if (cfg_.device_id.empty()) {
        cfg_.device_id = randomMac();
    }
    if (cfg_.client_id.empty()) {
        cfg_.client_id = randomUuidV4();
    }

    std::ofstream out(identity_file, std::ios::trunc);
    if (out.good()) {
        out << "DEVICE_ID=" << cfg_.device_id << "\n";
        out << "CLIENT_ID=" << cfg_.client_id << "\n";
    }

    std::string mac_no_colon;
    for (char c : cfg_.device_id) {
        if (c != ':') {
            mac_no_colon.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    const std::string md5 = pythonDigest("md5", mac_no_colon);
    std::string short_hash = md5.size() >= 8 ? md5.substr(0, 8) : "00000000";
    for (char& c : short_hash) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    serial_number_ = "SN-" + short_hash + "-" + mac_no_colon;

    hmac_key_ = pythonDigest("sha256", cfg_.device_id);
    if (hmac_key_.empty()) {
        hmac_key_ = pythonDigest("sha256", serial_number_);
    }
}

OtaStatus OtaClient::check() {
    OtaStatus st;

    const std::string body = makeRequestBody();
    const std::string resp = postJson(cfg_.ota_url, body);
    if (resp.empty()) {
        st.error = "ota response empty";
        return st;
    }

    st.ok = true;
    st.activation_code = extract(resp, "\\\"code\\\"\\s*:\\s*\\\"([0-9A-Za-z]{4,8})\\\"");
    st.activation_challenge = extract(resp, "\\\"challenge\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"");
    st.ws_url = extract(resp, "\\\"websocket\\\"[\\s\\S]*?\\\"url\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"");
    st.ws_token = extract(resp, "\\\"websocket\\\"[\\s\\S]*?\\\"token\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");

    if (st.activation_code.empty() && st.ws_url.empty()) {
        st.error = resp.substr(0, 240);
        std::cout << "[ota] unexpected response: " << st.error << std::endl;
    }

    if (!st.activation_code.empty()) {
        st.needs_binding = true;
        return st;
    }

    if (!st.ws_url.empty()) {
        st.paired = true;
    }

    return st;
}

const std::string& OtaClient::deviceId() const {
    return cfg_.device_id;
}

const std::string& OtaClient::clientId() const {
    return cfg_.client_id;
}

ActivateResult OtaClient::activate(const std::string& challenge) {
    if (challenge.empty() || hmac_key_.empty() || serial_number_.empty()) {
        return ActivateResult::Error;
    }

    const std::string signature = pythonHmacSha256(hmac_key_, challenge);
    if (signature.empty()) {
        return ActivateResult::Error;
    }

    std::ostringstream body;
    body << "{";
    body << "\"Payload\":{";
    body << "\"algorithm\":\"hmac-sha256\",";
    body << "\"serial_number\":\"" << serial_number_ << "\",";
    body << "\"challenge\":\"" << challenge << "\",";
    body << "\"hmac\":\"" << signature << "\"";
    body << "}";
    body << "}";

    const int code = postJsonCode(cfg_.ota_url + "activate", body.str());
    if (code == 200) {
        return ActivateResult::Ok;
    }
    if (code == 202) {
        return ActivateResult::Pending;
    }
    return ActivateResult::Error;
}

std::string OtaClient::makeRequestBody() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"application\":{";
    oss << "\"version\":\"1.0.0\",";
    oss << "\"elf_sha256\":\"simulator\"";
    oss << "},";
    oss << "\"board\":{";
    oss << "\"type\":\"cardputer-sim\",";
    oss << "\"name\":\"cardputer-xiaozhi\",";
    oss << "\"ip\":\"127.0.0.1\",";
    oss << "\"mac\":\"" << cfg_.device_id << "\"";
    oss << "}";
    oss << "}";
    return oss.str();
}

std::string OtaClient::postJson(const std::string& url, const std::string& body) const {
    const std::string cmd =
        "curl -sS -X POST " + shellEscape(url) +
        " -H " + shellEscape("Device-Id: " + cfg_.device_id) +
        " -H " + shellEscape("Client-Id: " + cfg_.client_id) +
        " -H " + shellEscape("Content-Type: application/json") +
        " -H " + shellEscape("Accept-Language: zh-CN") +
        " -H " + shellEscape("Activation-Version: 1.0.0") +
        " --data " + shellEscape(body) +
        " 2>&1";

    std::array<char, 512> buffer{};
    std::string output;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr) {
        return {};
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }
    pclose(pipe);
    return output;
}

int OtaClient::postJsonCode(const std::string& url, const std::string& body) const {
    const std::string cmd =
        "curl -sS -o /dev/null -w '%{http_code}' -X POST " + shellEscape(url) +
        " -H " + shellEscape("Device-Id: " + cfg_.device_id) +
        " -H " + shellEscape("Client-Id: " + cfg_.client_id) +
        " -H " + shellEscape("Content-Type: application/json") +
        " -H " + shellEscape("Accept-Language: zh-CN") +
        " -H " + shellEscape("Activation-Version: 1.0.0") +
        " --data " + shellEscape(body);

    std::array<char, 64> buf{};
    std::string out;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr) {
        return 0;
    }
    while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
        out += buf.data();
    }
    pclose(pipe);

    out = trim(out);
    if (out.empty()) {
        return 0;
    }
    return std::atoi(out.c_str());
}

std::string OtaClient::shellEscape(const std::string& value) const {
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out.push_back(c);
        }
    }
    out += "'";
    return out;
}

std::string OtaClient::extract(const std::string& text, const std::string& regex_expr) const {
    std::smatch match;
    const std::regex re(regex_expr, std::regex::ECMAScript);
    if (std::regex_search(text, match, re) && match.size() > 1) {
        return match[1].str();
    }
    return {};
}

std::string OtaClient::pythonDigest(const std::string& algo, const std::string& text) const {
    auto pyQuote = [](const std::string& s) {
        std::string out;
        out.reserve(s.size() + 8);
        for (char c : s) {
            if (c == '\\' || c == '\'') {
                out.push_back('\\');
            }
            out.push_back(c);
        }
        return std::string("'") + out + "'";
    };

    const std::string script =
        "import hashlib;print(hashlib.new(" + pyQuote(algo) + "," + pyQuote(text) + ".encode()).hexdigest())";
    const std::string cmd =
        "python3 -c " + shellEscape(script);
    std::array<char, 256> buf{};
    std::string out;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr) {
        return {};
    }
    while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
        out += buf.data();
    }
    pclose(pipe);
    return trim(out);
}

std::string OtaClient::pythonHmacSha256(const std::string& key, const std::string& text) const {
    auto pyQuote = [](const std::string& s) {
        std::string out;
        out.reserve(s.size() + 8);
        for (char c : s) {
            if (c == '\\' || c == '\'') {
                out.push_back('\\');
            }
            out.push_back(c);
        }
        return std::string("'") + out + "'";
    };

    const std::string script = "import hmac,hashlib;print(hmac.new(" + pyQuote(key) + ".encode()," +
                               pyQuote(text) + ".encode(),hashlib.sha256).hexdigest())";
    const std::string cmd = "python3 -c " + shellEscape(script);
    std::array<char, 256> buf{};
    std::string out;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr) {
        return {};
    }
    while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
        out += buf.data();
    }
    pclose(pipe);
    return trim(out);
}


}  // namespace xiaozhi
