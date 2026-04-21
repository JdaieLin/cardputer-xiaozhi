#pragma once

#include <cstdint>
#include <vector>

namespace xiaozhi {

class AudioPipeline {
public:
    virtual ~AudioPipeline() = default;
    virtual bool init() = 0;
    virtual void startCapture() = 0;
    virtual void stopCapture() = 0;
    virtual bool isCapturing() const = 0;
    virtual std::vector<int16_t> readPcmFrame() = 0;
    virtual void playPcmFrame(const std::vector<int16_t>& pcm) = 0;
    virtual bool hasPlaybackDevice() const { return false; }
};

class AudioPipelineStub final : public AudioPipeline {
public:
    bool init() override;
    void startCapture() override;
    void stopCapture() override;
    bool isCapturing() const override;
    std::vector<int16_t> readPcmFrame() override;
    void playPcmFrame(const std::vector<int16_t>& pcm) override;

private:
    bool capturing_ = false;
};

}  // namespace xiaozhi
