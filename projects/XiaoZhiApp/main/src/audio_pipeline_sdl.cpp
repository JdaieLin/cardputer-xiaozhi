#include "audio_pipeline_sdl.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>

namespace xiaozhi {
namespace {

constexpr int kTargetRate = 16000;
constexpr int kTargetChannels = 1;
constexpr int kFrameSamples = 320;  // 20 ms @ 16k mono

}  // namespace

AudioPipelineSdl::~AudioPipelineSdl() {
    stopCapture();

    if (capture_device_ != 0) {
        SDL_CloseAudioDevice(capture_device_);
        capture_device_ = 0;
    }

    if (playback_device_ != 0) {
        SDL_CloseAudioDevice(playback_device_);
        playback_device_ = 0;
    }
}

bool AudioPipelineSdl::init() {
    SDL_AudioSpec desired{};
    desired.freq = kTargetRate;
    desired.format = AUDIO_S16SYS;
    desired.channels = static_cast<Uint8>(kTargetChannels);
    desired.samples = 1024;
    desired.callback = nullptr;

    capture_device_ = SDL_OpenAudioDevice(nullptr, 1, &desired, &capture_spec_, SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (capture_device_ == 0) {
        std::cerr << "[audio-sdl] capture open failed: " << SDL_GetError() << std::endl;
        return false;
    }

    playback_device_ = SDL_OpenAudioDevice(nullptr, 0, &desired, &playback_spec_, SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (playback_device_ == 0) {
        std::cerr << "[audio-sdl] playback open failed: " << SDL_GetError() << std::endl;
        SDL_CloseAudioDevice(capture_device_);
        capture_device_ = 0;
        return false;
    }

    SDL_PauseAudioDevice(playback_device_, 0);

    std::cout << "[audio-sdl] initialized capture=" << capture_spec_.freq << "Hz/" << static_cast<int>(capture_spec_.channels)
              << "ch playback=" << playback_spec_.freq << "Hz/" << static_cast<int>(playback_spec_.channels) << "ch"
              << std::endl;
    return true;
}

void AudioPipelineSdl::startCapture() {
    if (capture_device_ == 0) {
        return;
    }

    SDL_ClearQueuedAudio(capture_device_);
    SDL_PauseAudioDevice(capture_device_, 0);
    capturing_ = true;
    std::cout << "[audio-sdl] capture started" << std::endl;
}

void AudioPipelineSdl::stopCapture() {
    if (!capturing_) {
        return;
    }

    if (capture_device_ != 0) {
        SDL_PauseAudioDevice(capture_device_, 1);
    }
    capturing_ = false;
    std::cout << "[audio-sdl] capture stopped" << std::endl;
}

bool AudioPipelineSdl::isCapturing() const {
    return capturing_;
}

std::vector<int16_t> AudioPipelineSdl::readPcmFrame() {
    if (!capturing_ || capture_device_ == 0) {
        return {};
    }

    const Uint32 required_bytes = static_cast<Uint32>(kFrameSamples * sizeof(int16_t));
    const Uint32 queued = SDL_GetQueuedAudioSize(capture_device_);
    if (queued < required_bytes) {
        return {};
    }

    std::vector<int16_t> frame(kFrameSamples, 0);
    const Uint32 read = SDL_DequeueAudio(capture_device_, frame.data(), required_bytes);
    if (read != required_bytes) {
        frame.resize(read / sizeof(int16_t));
    }

    return frame;
}

void AudioPipelineSdl::playPcmFrame(const std::vector<int16_t>& pcm) {
    if (playback_device_ == 0 || pcm.empty()) {
        return;
    }

    SDL_QueueAudio(playback_device_, pcm.data(), static_cast<Uint32>(pcm.size() * sizeof(int16_t)));

    // Keep playback latency bounded in simulator.
    const Uint32 max_queue = static_cast<Uint32>(kTargetRate * sizeof(int16_t) / 2);
    if (SDL_GetQueuedAudioSize(playback_device_) > max_queue) {
        SDL_ClearQueuedAudio(playback_device_);
    }
}

bool AudioPipelineSdl::hasPlaybackDevice() const {
    return playback_device_ != 0;
}

}  // namespace xiaozhi
