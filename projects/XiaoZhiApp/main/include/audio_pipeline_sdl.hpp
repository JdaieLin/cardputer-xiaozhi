#pragma once

#include <SDL.h>

#include <vector>

#include "audio_pipeline.hpp"

namespace xiaozhi {

class AudioPipelineSdl final : public AudioPipeline {
public:
    AudioPipelineSdl() = default;
    ~AudioPipelineSdl() override;

    bool init() override;
    void startCapture() override;
    void stopCapture() override;
    bool isCapturing() const override;
    std::vector<int16_t> readPcmFrame() override;
    void playPcmFrame(const std::vector<int16_t>& pcm) override;
    bool hasPlaybackDevice() const override;

private:
    SDL_AudioDeviceID capture_device_ = 0;
    SDL_AudioDeviceID playback_device_ = 0;
    SDL_AudioSpec capture_spec_{};
    SDL_AudioSpec playback_spec_{};
    bool capturing_ = false;
};

}  // namespace xiaozhi
