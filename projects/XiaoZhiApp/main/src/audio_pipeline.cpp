#include "audio_pipeline.hpp"

#include <iostream>

namespace xiaozhi {

bool AudioPipelineStub::init() {
    std::cout << "[audio] init stub" << std::endl;
    return true;
}

void AudioPipelineStub::startCapture() {
    capturing_ = true;
    std::cout << "[audio] capture started" << std::endl;
}

void AudioPipelineStub::stopCapture() {
    capturing_ = false;
    std::cout << "[audio] capture stopped" << std::endl;
}

bool AudioPipelineStub::isCapturing() const {
    return capturing_;
}

std::vector<int16_t> AudioPipelineStub::readPcmFrame() {
    if (!capturing_) {
        return {};
    }

    // Simulated 20 ms mono frame at 16 kHz.
    return std::vector<int16_t>(320, 0);
}

void AudioPipelineStub::playPcmFrame(const std::vector<int16_t>& pcm) {
    (void)pcm;
}

}  // namespace xiaozhi
