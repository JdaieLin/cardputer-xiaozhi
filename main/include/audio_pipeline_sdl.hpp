#pragma once

#include <SDL.h>

#include <cstdio>
#include <sys/types.h>
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
    void closeDebugCapture();
    bool startExternalCapture();
    void stopExternalCapture();
    bool useExternalCapture() const;

    SDL_AudioDeviceID capture_device_ = 0;
    SDL_AudioDeviceID playback_device_ = 0;
    SDL_AudioSpec capture_spec_{};
    SDL_AudioSpec playback_spec_{};
    bool capturing_ = false;
    bool external_capture_ = false;
    int capture_pipe_fd_ = -1;
    pid_t capture_pid_ = -1;
    FILE* debug_raw_ = nullptr;
    int debug_raw_count_ = 0;
    int total_tts_frames_ = 0;
};

}  // namespace xiaozhi
