#include "display_bridge.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include "config.hpp"

namespace xiaozhi {
namespace {

std::string bridgeScriptPath() {
    if (const char* override_path = std::getenv("XIAOZHI_DISPLAY_BRIDGE"); override_path != nullptr && *override_path != '\0') {
        if (access(override_path, R_OK) == 0) {
            return override_path;
        }
    }

    const char* ws_bridge_env = std::getenv("XIAOZHI_WS_BRIDGE");
    if (ws_bridge_env != nullptr && *ws_bridge_env != '\0') {
        std::filesystem::path ws_path(ws_bridge_env);
        std::filesystem::path dir = ws_path.parent_path();
        std::filesystem::path display_path = dir / "display_bridge.py";
        if (access(display_path.c_str(), R_OK) == 0) {
            return display_path.string();
        }
    }

    std::array<char, 4096> buf{};
    const ssize_t size = ::readlink("/proc/self/exe", buf.data(), buf.size() - 1);
    if (size > 0) {
        buf[static_cast<size_t>(size)] = '\0';
        std::filesystem::path exe(buf.data());
        std::filesystem::path dir = exe.parent_path().parent_path() / "share" / "xiaozhi";
        if (access(dir.c_str(), F_OK) == 0) {
            std::filesystem::path script = dir / "display_bridge.py";
            if (access(script.c_str(), R_OK) == 0) {
                return script.string();
            }
        }
    }

    return "display_bridge.py";
}

std::string python3Path() {
    for (const auto& candidate : {"/usr/bin/python3", "/usr/local/bin/python3"}) {
        if (access(candidate, X_OK) == 0) return candidate;
    }
    return "python3";
}

}  // namespace

static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char hex[8];
                    snprintf(hex, sizeof(hex), "\\u%04x", static_cast<unsigned char>(c));
                    out += hex;
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    return out;
}

DisplayBridge::DisplayBridge() = default;

DisplayBridge::~DisplayBridge() {
    disconnect();
}

bool DisplayBridge::init() {
    signal(SIGPIPE, SIG_IGN);

    int to_child[2] = {-1, -1};
    int from_child[2] = {-1, -1};
    if (pipe(to_child) != 0 || pipe(from_child) != 0) {
        std::cerr << "[display-bridge] pipe failed" << std::endl;
        return false;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "[display-bridge] fork failed" << std::endl;
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
            std::cerr << "[display-bridge] display_bridge.py not found" << std::endl;
            _exit(1);
        }

        execl(python_path.c_str(), python_path.c_str(), script_path.c_str(), static_cast<char*>(nullptr));
        execlp("python3", "python3", script_path.c_str(), static_cast<char*>(nullptr));
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

    std::cout << "[display-bridge] started, waiting for handshake" << std::endl;

    std::string line_buffer;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (std::chrono::steady_clock::now() < deadline) {
        char buf[512];
        const ssize_t n = read(child_stdout_fd_, buf, sizeof(buf));
        if (n == 0) {
            std::cerr << "[display-bridge] exited before handshake" << std::endl;
            disconnect();
            return false;
        }
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(20 * 1000);
                continue;
            }
            std::cerr << "[display-bridge] handshake read failed: " << std::strerror(errno) << std::endl;
            disconnect();
            return false;
        }

        line_buffer.append(buf, static_cast<size_t>(n));
        size_t pos;
        while ((pos = line_buffer.find('\n')) != std::string::npos) {
            std::string line = line_buffer.substr(0, pos);
            line_buffer.erase(0, pos + 1);
            if (line.find("\"connected\"") != std::string::npos) {
                std::cout << "[display-bridge] handshake ready" << std::endl;
                connected_ = true;
                // Drain child stdout/stderr pipe in background to prevent pipe-buffer deadlock.
                std::thread([fd = child_stdout_fd_]() {
                    char buf[512];
                    while (true) {
                        ssize_t n = read(fd, buf, sizeof(buf));
                        if (n <= 0) break;
                    }
                }).detach();
                return true;
            }
        }
    }

    std::cerr << "[display-bridge] handshake timeout" << std::endl;
    disconnect();
    return false;
}

void DisplayBridge::disconnect() {
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
    connected_ = false;
}

void DisplayBridge::sendJsonLine(const std::string& json_line) {
    if (child_stdin_fd_ < 0 || !connected_) return;
    const std::string line = json_line + "\n";
    const ssize_t n = write(child_stdin_fd_, line.c_str(), line.size());
    static_cast<void>(n);
}

void DisplayBridge::renderState(AppState state, const std::string& text, const std::string& emoji) {
    if (!connected_) return;

    const char* status_str = "IDLE";
    switch (state) {
        case AppState::Binding:   status_str = "BINDING"; break;
        case AppState::Idle:      status_str = "IDLE"; break;
        case AppState::Listening: status_str = "LISTENING"; break;
        case AppState::Thinking:  status_str = "THINKING"; break;
        case AppState::Speaking:  status_str = "SPEAKING"; break;
        case AppState::Error:     status_str = "ERROR"; break;
    }

    std::string emoji_val = emoji.empty() ? "😄" : emoji;

    std::string code;
    for (char c : text) {
        if (c >= '0' && c <= '9') code.push_back(c);
    }

    std::string json = "{\"cmd\":\"render\",\"status\":\"" + std::string(status_str) +
                       "\",\"emoji\":\"" + jsonEscape(emoji_val) +
                       "\",\"text\":\"" + jsonEscape(text) +
                       "\",\"code\":\"" + (code.size() == 6 ? code : "") + "\"}";
    sendJsonLine(json);
}

}  // namespace xiaozhi
