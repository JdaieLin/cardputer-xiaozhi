#include "ws_client.hpp"

#include <fcntl.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <regex>
#include <sstream>
#include <vector>

namespace xiaozhi {

namespace {

std::filesystem::path executablePath() {
#if defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(static_cast<size_t>(size), '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return {};
    }
    return std::filesystem::weakly_canonical(std::filesystem::path(buffer.c_str()));
#elif defined(__linux__)
    std::array<char, 4096> buffer{};
    const ssize_t size = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (size <= 0) {
        return {};
    }
    buffer[static_cast<size_t>(size)] = '\0';
    return std::filesystem::weakly_canonical(std::filesystem::path(buffer.data()));
#else
    return {};
#endif
}

std::filesystem::path projectDirFromExecutable() {
    const std::filesystem::path exe = executablePath();
    if (exe.empty()) {
        return {};
    }

    const std::filesystem::path candidate = exe.parent_path().parent_path();
    if (std::filesystem::exists(candidate / "main" / "tools" / "ws_bridge.py")) {
        return candidate;
    }
    return {};
}

std::string bridgeScriptPath() {
    if (const char* override_path = std::getenv("XIAOZHI_WS_BRIDGE"); override_path != nullptr && *override_path != '\0') {
        if (access(override_path, R_OK) == 0) {
            return override_path;
        }
    }

    const std::filesystem::path project_dir = projectDirFromExecutable();
    if (!project_dir.empty()) {
        return (project_dir / "main" / "tools" / "ws_bridge.py").string();
    }

    std::filesystem::path base = executablePath().parent_path();
    const std::vector<std::filesystem::path> sibling_candidates = {
        std::filesystem::path("cardputer-xiaozhi/main/tools/ws_bridge.py"),
        std::filesystem::path("../cardputer-xiaozhi/main/tools/ws_bridge.py"),
    };
    for (int depth = 0; depth < 8 && !base.empty(); ++depth) {
        for (const auto& suffix : sibling_candidates) {
            const auto candidate = (base / suffix).lexically_normal();
            if (access(candidate.c_str(), R_OK) == 0) {
                return candidate.string();
            }
        }
        const auto parent = base.parent_path();
        if (parent == base) {
            break;
        }
        base = parent;
    }

    if (access("main/tools/ws_bridge.py", R_OK) == 0) {
        return "main/tools/ws_bridge.py";
    }
    if (access("../main/tools/ws_bridge.py", R_OK) == 0) {
        return "../main/tools/ws_bridge.py";
    }
    return {};
}

std::string python3Path() {
    static const std::vector<std::string> candidates = {
        "/opt/homebrew/bin/python3",
        "/usr/local/bin/python3",
        "/usr/bin/python3",
    };
    for (const auto& candidate : candidates) {
        if (access(candidate.c_str(), X_OK) == 0) {
            return candidate;
        }
    }

    std::array<char, 256> buf{};
    std::string out;
    FILE* pipe = popen("which python3 2>/dev/null", "r");
    if (pipe != nullptr) {
        while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
            out += buf.data();
        }
        pclose(pipe);
    }
    if (!out.empty()) {
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) {
            out.pop_back();
        }
        if (access(out.c_str(), X_OK) == 0) {
            return out;
        }
    }

    return {};
}

std::string appendUtf8(std::string out, unsigned codepoint) {
    if (codepoint <= 0x7F) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    return out;
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    return -1;
}

std::string jsonUnescape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 >= text.size()) {
            out.push_back(text[i]);
            continue;
        }

        const char esc = text[++i];
        switch (esc) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                if (i + 4 >= text.size()) {
                    out += "\\u";
                    break;
                }
                unsigned codepoint = 0;
                bool ok = true;
                for (size_t j = 0; j < 4; ++j) {
                    const int value = hexValue(text[i + 1 + j]);
                    if (value < 0) {
                        ok = false;
                        break;
                    }
                    codepoint = (codepoint << 4) | static_cast<unsigned>(value);
                }
                if (!ok) {
                    out += "\\u";
                    break;
                }
                i += 4;
                if (codepoint >= 0xD800 && codepoint <= 0xDBFF &&
                    i + 6 < text.size() && text[i + 1] == '\\' && text[i + 2] == 'u') {
                    unsigned low = 0;
                    bool low_ok = true;
                    for (size_t j = 0; j < 4; ++j) {
                        const int value = hexValue(text[i + 3 + j]);
                        if (value < 0) {
                            low_ok = false;
                            break;
                        }
                        low = (low << 4) | static_cast<unsigned>(value);
                    }
                    if (low_ok && low >= 0xDC00 && low <= 0xDFFF) {
                        codepoint = 0x10000 + (((codepoint - 0xD800) << 10) | (low - 0xDC00));
                        i += 6;
                    }
                }
                out = appendUtf8(std::move(out), codepoint);
                break;
            }
            default:
                out.push_back(esc);
                break;
        }
    }
    return out;
}

}  // namespace

bool WsClientStub::connect(const std::string& url,
                           const std::string& token,
                           const std::string& device_id,
                           const std::string& client_id) {
    (void)token;
    (void)device_id;
    (void)client_id;
    std::cout << "[ws] connect stub: " << url << std::endl;
    return true;
}

void WsClientStub::disconnect() {
    std::cout << "[ws] disconnect stub" << std::endl;
}

void WsClientStub::poll() {
    ++poll_count_;
}

void WsClientStub::sendAudioFrame(const std::vector<int16_t>& pcm) {
    (void)pcm;
}

void WsClientStub::setOnServerText(std::function<void(const std::string&)> cb) {
    on_server_text_ = std::move(cb);
}

void WsClientStub::setOnEmotion(std::function<void(const std::string&)> cb) {
    on_emotion_ = std::move(cb);
}

void WsClientStub::setOnTtsText(std::function<void(const std::string&)> cb) {
    on_tts_text_ = std::move(cb);
}

void WsClientStub::setOnListenStop(std::function<void()> cb) {
    on_listen_stop_ = std::move(cb);
}

void WsClientStub::setOnTtsStart(std::function<void()> cb) {
    on_tts_start_ = std::move(cb);
}

void WsClientStub::setOnTtsPcm(std::function<void(const std::vector<int16_t>&)> cb) {
    on_tts_pcm_ = std::move(cb);
}

void WsClientStub::setOnTtsStop(std::function<void()> cb) {
    on_tts_stop_ = std::move(cb);
}

void WsClientStub::setOnGoodbye(std::function<void()> cb) {
    on_goodbye_ = std::move(cb);
}

void WsClientStub::setOnDisconnected(std::function<void()> cb) {
    on_disconnected_ = std::move(cb);
}

WsClientBridge::WsClientBridge() = default;

WsClientBridge::~WsClientBridge() {
    disconnect();
}

bool WsClientBridge::connect(const std::string& url,
                             const std::string& token,
                             const std::string& device_id,
                             const std::string& client_id) {
    // Writing to a dead bridge pipe should not terminate the whole simulator.
    signal(SIGPIPE, SIG_IGN);

    disconnect();

    int to_child[2] = {-1, -1};
    int from_child[2] = {-1, -1};
    if (pipe(to_child) != 0 || pipe(from_child) != 0) {
        std::cerr << "[ws-bridge] pipe failed" << std::endl;
        return false;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "[ws-bridge] fork failed" << std::endl;
        return false;
    }

    if (pid == 0) {
        const std::string script_path = bridgeScriptPath();
        const std::string python_path = python3Path();

        dup2(to_child[0], STDIN_FILENO);
        dup2(from_child[1], STDOUT_FILENO);
        dup2(from_child[1], STDERR_FILENO);
        close(to_child[1]);
        close(from_child[0]);

        if (script_path.empty()) {
            std::cerr << "[ws-bridge] ws_bridge.py not found" << std::endl;
            _exit(1);
        }

        if (!python_path.empty()) {
            execl(python_path.c_str(),
                  python_path.c_str(),
                  script_path.c_str(),
                  "--url",
                  url.c_str(),
                  "--token",
                  token.c_str(),
                  "--device-id",
                  device_id.c_str(),
                  "--client-id",
                  client_id.c_str(),
                  static_cast<char*>(nullptr));
        }

        execlp("python3",
               "python3",
               script_path.c_str(),
               "--url",
               url.c_str(),
               "--token",
               token.c_str(),
               "--device-id",
               device_id.c_str(),
               "--client-id",
               client_id.c_str(),
               static_cast<char*>(nullptr));
        _exit(1);
    }

    child_pid_ = static_cast<int>(pid);
    child_stdin_fd_ = to_child[1];
    child_stdout_fd_ = from_child[0];

    close(to_child[0]);
    close(from_child[1]);

    const int flags = fcntl(child_stdout_fd_, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(child_stdout_fd_, F_SETFL, flags | O_NONBLOCK);
    }

    std::cout << "[ws-bridge] started, waiting for handshake" << std::endl;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (std::chrono::steady_clock::now() < deadline) {
        char buf[512];
        const ssize_t n = read(child_stdout_fd_, buf, sizeof(buf));
        if (n == 0) {
            std::cerr << "[ws-bridge] exited before handshake" << std::endl;
            disconnect();
            return false;
        }
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(20 * 1000);
                continue;
            }
            std::cerr << "[ws-bridge] handshake read failed: " << std::strerror(errno) << std::endl;
            disconnect();
            return false;
        }

        line_buffer_.append(buf, static_cast<size_t>(n));
        while (true) {
            const size_t pos = line_buffer_.find('\n');
            if (pos == std::string::npos) {
                break;
            }

            std::string line = line_buffer_.substr(0, pos);
            line_buffer_.erase(0, pos + 1);
            const std::string event = extractField(line, "event");
            if (event == "connected") {
                std::cout << "[ws-bridge] handshake ready" << std::endl;
                return true;
            }
            if (event == "error") {
                std::cerr << "[ws-bridge] startup error: " << line << std::endl;
                disconnect();
                return false;
            }
        }
    }

    std::cerr << "[ws-bridge] handshake timeout" << std::endl;
    disconnect();
    return false;
}

void WsClientBridge::disconnect() {
    if (child_stdin_fd_ >= 0) {
        close(child_stdin_fd_);
        child_stdin_fd_ = -1;
    }
    if (child_stdout_fd_ >= 0) {
        close(child_stdout_fd_);
        child_stdout_fd_ = -1;
    }
    if (child_pid_ > 0) {
        kill(child_pid_, SIGTERM);
        int status = 0;
        waitpid(child_pid_, &status, WNOHANG);
        child_pid_ = -1;
    }
}

void WsClientBridge::notifyDisconnected() {
    if (on_disconnected_) {
        on_disconnected_();
    }
}

void WsClientBridge::poll() {
    if (child_stdout_fd_ < 0) {
        return;
    }

    char buf[2048];
    while (true) {
        const ssize_t n = read(child_stdout_fd_, buf, sizeof(buf));
        if (n == 0) {
            disconnect();
            notifyDisconnected();
            break;
        }
        if (n < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                disconnect();
                notifyDisconnected();
            }
            break;
        }

        line_buffer_.append(buf, static_cast<size_t>(n));
        while (true) {
            const size_t pos = line_buffer_.find('\n');
            if (pos == std::string::npos) {
                break;
            }
            std::string line = line_buffer_.substr(0, pos);
            line_buffer_.erase(0, pos + 1);
            handleEventLine(line);
        }
    }
}

void WsClientBridge::sendListenStart() {
    sendJsonLine("{\"cmd\":\"listen_start\"}");
}

void WsClientBridge::sendListenStop() {
    sendJsonLine("{\"cmd\":\"listen_stop\"}");
}

void WsClientBridge::sendAbort() {
    sendJsonLine("{\"cmd\":\"abort\"}");
}

void WsClientBridge::sendAudioFrame(const std::vector<int16_t>& pcm) {
    if (pcm.empty()) {
        return;
    }

    std::vector<unsigned char> bytes(pcm.size() * sizeof(int16_t));
    std::memcpy(bytes.data(), pcm.data(), bytes.size());
    const std::string b64 = b64Encode(bytes);
    sendJsonLine("{\"cmd\":\"audio\",\"pcm\":\"" + b64 + "\"}");
}

void WsClientBridge::setOnServerText(std::function<void(const std::string&)> cb) {
    on_server_text_ = std::move(cb);
}

void WsClientBridge::setOnEmotion(std::function<void(const std::string&)> cb) {
    on_emotion_ = std::move(cb);
}

void WsClientBridge::setOnTtsText(std::function<void(const std::string&)> cb) {
    on_tts_text_ = std::move(cb);
}

void WsClientBridge::setOnListenStop(std::function<void()> cb) {
    on_listen_stop_ = std::move(cb);
}

void WsClientBridge::setOnTtsStart(std::function<void()> cb) {
    on_tts_start_ = std::move(cb);
}

void WsClientBridge::setOnTtsPcm(std::function<void(const std::vector<int16_t>&)> cb) {
    on_tts_pcm_ = std::move(cb);
}

void WsClientBridge::setOnTtsStop(std::function<void()> cb) {
    on_tts_stop_ = std::move(cb);
}

void WsClientBridge::setOnGoodbye(std::function<void()> cb) {
    on_goodbye_ = std::move(cb);
}

void WsClientBridge::setOnDisconnected(std::function<void()> cb) {
    on_disconnected_ = std::move(cb);
}

void WsClientBridge::sendJsonLine(const std::string& json_line) {
    if (child_stdin_fd_ < 0) {
        return;
    }
    const std::string line = json_line + "\n";
    const ssize_t n = write(child_stdin_fd_, line.c_str(), line.size());
    if (n < 0) {
        if (errno == EPIPE || errno == EBADF) {
            std::cerr << "[ws-bridge] pipe closed, disconnecting bridge" << std::endl;
            disconnect();
            notifyDisconnected();
        } else {
            std::cerr << "[ws-bridge] write failed: " << std::strerror(errno) << std::endl;
        }
    }
}

void WsClientBridge::handleEventLine(const std::string& line) {
    if (line.empty()) {
        return;
    }

    const std::string event = extractField(line, "event");
    if (event.empty()) {
        return;
    }

    if (event == "stt") {
        const std::string text = extractField(line, "text");
        if (on_server_text_ && !text.empty()) {
            on_server_text_(text);
        }
        return;
    }

    if (event == "tts_start") {
        if (on_tts_start_) {
            on_tts_start_();
        }
        return;
    }

    if (event == "llm_emotion") {
        const std::string emoji = extractField(line, "emoji");
        if (on_emotion_ && !emoji.empty()) {
            on_emotion_(emoji);
        }
        return;
    }

    if (event == "tts_text") {
        const std::string text = extractField(line, "text");
        if (on_tts_text_ && !text.empty()) {
            on_tts_text_(text);
        }
        return;
    }

    if (event == "listen_stop") {
        if (on_listen_stop_) {
            on_listen_stop_();
        }
        return;
    }

    if (event == "tts_stop") {
        if (on_tts_stop_) {
            on_tts_stop_();
        }
        return;
    }

    if (event == "goodbye") {
        if (on_goodbye_) {
            on_goodbye_();
        }
        return;
    }

    if (event == "tts_audio") {
        const std::string b64 = extractField(line, "pcm");
        if (b64.empty() || !on_tts_pcm_) {
            return;
        }

        const std::vector<unsigned char> bytes = b64Decode(b64);
        if (bytes.empty()) {
            return;
        }

        std::vector<int16_t> pcm(bytes.size() / sizeof(int16_t));
        std::memcpy(pcm.data(), bytes.data(), pcm.size() * sizeof(int16_t));
        on_tts_pcm_(pcm);
        return;
    }

    if (event == "error") {
        std::cout << "[ws-bridge] " << line << std::endl;
    }
}

std::string WsClientBridge::extractField(const std::string& line, const std::string& key) const {
    const std::regex re("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch m;
    if (std::regex_search(line, m, re) && m.size() > 1) {
        return jsonUnescape(m[1].str());
    }
    return {};
}

std::string WsClientBridge::b64Encode(const std::vector<unsigned char>& data) const {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);

    size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        const unsigned v = (static_cast<unsigned>(data[i]) << 16) |
                           (static_cast<unsigned>(data[i + 1]) << 8) |
                           static_cast<unsigned>(data[i + 2]);
        out.push_back(tbl[(v >> 18) & 0x3f]);
        out.push_back(tbl[(v >> 12) & 0x3f]);
        out.push_back(tbl[(v >> 6) & 0x3f]);
        out.push_back(tbl[v & 0x3f]);
    }

    if (i < data.size()) {
        unsigned v = static_cast<unsigned>(data[i]) << 16;
        out.push_back(tbl[(v >> 18) & 0x3f]);
        if (i + 1 < data.size()) {
            v |= static_cast<unsigned>(data[i + 1]) << 8;
            out.push_back(tbl[(v >> 12) & 0x3f]);
            out.push_back(tbl[(v >> 6) & 0x3f]);
            out.push_back('=');
        } else {
            out.push_back(tbl[(v >> 12) & 0x3f]);
            out.push_back('=');
            out.push_back('=');
        }
    }
    return out;
}

std::vector<unsigned char> WsClientBridge::b64Decode(const std::string& in) const {
    static const std::string tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<unsigned char> out;
    int val = 0;
    int bits = -8;
    for (const unsigned char c : in) {
        if (std::isspace(c) != 0) {
            continue;
        }
        if (c == '=') {
            break;
        }
        const size_t p = tbl.find(static_cast<char>(c));
        if (p == std::string::npos) {
            continue;
        }
        val = (val << 6) + static_cast<int>(p);
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<unsigned char>((val >> bits) & 0xff));
            bits -= 8;
        }
    }
    return out;
}

}  // namespace xiaozhi
