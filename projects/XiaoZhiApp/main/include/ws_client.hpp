#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace xiaozhi {

class WsClient {
public:
    virtual ~WsClient() = default;
    virtual bool connect(const std::string& url,
                         const std::string& token,
                         const std::string& device_id,
                         const std::string& client_id) = 0;
    virtual void disconnect() = 0;
    virtual void poll() = 0;
    virtual void sendListenStart() {}
    virtual void sendListenStop() {}
    virtual void sendAbort() {}
    virtual void sendAudioFrame(const std::vector<int16_t>& pcm) = 0;
    virtual void setOnServerText(std::function<void(const std::string&)> cb) = 0;
    virtual void setOnListenStop(std::function<void()> cb) { (void)cb; }
    virtual void setOnTtsStart(std::function<void()> cb) = 0;
    virtual void setOnTtsPcm(std::function<void(const std::vector<int16_t>&)> cb) { (void)cb; }
    virtual void setOnTtsStop(std::function<void()> cb) = 0;
};

class WsClientStub final : public WsClient {
public:
    bool connect(const std::string& url,
                 const std::string& token,
                 const std::string& device_id,
                 const std::string& client_id) override;
    void disconnect() override;
    void poll() override;
    void sendAudioFrame(const std::vector<int16_t>& pcm) override;
    void setOnServerText(std::function<void(const std::string&)> cb) override;
    void setOnListenStop(std::function<void()> cb) override;
    void setOnTtsStart(std::function<void()> cb) override;
    void setOnTtsPcm(std::function<void(const std::vector<int16_t>&)> cb) override;
    void setOnTtsStop(std::function<void()> cb) override;

private:
    std::function<void(const std::string&)> on_server_text_;
    std::function<void()> on_listen_stop_;
    std::function<void()> on_tts_start_;
    std::function<void(const std::vector<int16_t>&)> on_tts_pcm_;
    std::function<void()> on_tts_stop_;
    int poll_count_ = 0;
};

class WsClientBridge final : public WsClient {
public:
    WsClientBridge();
    ~WsClientBridge() override;

    bool connect(const std::string& url,
                 const std::string& token,
                 const std::string& device_id,
                 const std::string& client_id) override;
    void disconnect() override;
    void poll() override;
    void sendListenStart() override;
    void sendListenStop() override;
    void sendAbort() override;
    void sendAudioFrame(const std::vector<int16_t>& pcm) override;
    void setOnServerText(std::function<void(const std::string&)> cb) override;
    void setOnListenStop(std::function<void()> cb) override;
    void setOnTtsStart(std::function<void()> cb) override;
    void setOnTtsPcm(std::function<void(const std::vector<int16_t>&)> cb) override;
    void setOnTtsStop(std::function<void()> cb) override;

private:
    void sendJsonLine(const std::string& json_line);
    void handleEventLine(const std::string& line);
    std::string extractField(const std::string& line, const std::string& key) const;
    std::string b64Encode(const std::vector<unsigned char>& data) const;
    std::vector<unsigned char> b64Decode(const std::string& in) const;

    int child_stdin_fd_ = -1;
    int child_stdout_fd_ = -1;
    int child_pid_ = -1;
    std::string line_buffer_;
    std::function<void(const std::string&)> on_server_text_;
    std::function<void()> on_listen_stop_;
    std::function<void()> on_tts_start_;
    std::function<void(const std::vector<int16_t>&)> on_tts_pcm_;
    std::function<void()> on_tts_stop_;
};

}  // namespace xiaozhi
