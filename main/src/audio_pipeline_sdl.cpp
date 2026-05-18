#include "audio_pipeline_sdl.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace xiaozhi {
namespace {

constexpr int kTargetRate = 16000;
constexpr int kMonoSamples = 320;  // 20 ms @ 16k
constexpr int kMonoBytes = kMonoSamples * static_cast<int>(sizeof(int16_t));

bool containsIgnoreCase(const std::string& text, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }

    auto it = std::search(text.begin(), text.end(), needle.begin(), needle.end(),
                          [](char lhs, char rhs) {
                              return std::tolower(static_cast<unsigned char>(lhs)) ==
                                     std::tolower(static_cast<unsigned char>(rhs));
                          });
    return it != text.end();
}

std::vector<std::string> candidateDeviceNames(int is_capture) {
    const char* env_name = is_capture ? std::getenv("XIAOZHI_AUDIO_CAPTURE_DEVICE")
                                      : std::getenv("XIAOZHI_AUDIO_PLAYBACK_DEVICE");
    const std::string requested = env_name != nullptr ? env_name : "";
    const int num = SDL_GetNumAudioDevices(is_capture);
    std::vector<std::string> names;
    names.reserve(static_cast<size_t>(std::max(num, 0)));
    for (int i = 0; i < num; i++) {
        const char* raw_name = SDL_GetAudioDeviceName(i, is_capture);
        if (raw_name != nullptr) {
            names.emplace_back(raw_name);
        }
    }

    std::vector<std::string> ordered;
    ordered.reserve(names.size());
    auto append_matches = [&](const auto& predicate) {
        for (const auto& name : names) {
            if (!predicate(name)) {
                continue;
            }
            if (std::find(ordered.begin(), ordered.end(), name) == ordered.end()) {
                ordered.push_back(name);
            }
        }
    };

    if (!requested.empty()) {
        append_matches([&](const std::string& name) {
            return containsIgnoreCase(name, requested);
        });
    }

    append_matches([](const std::string& name) {
        return containsIgnoreCase(name, "es8388");
    });

    append_matches([](const std::string& name) {
        return !containsIgnoreCase(name, "hdmi") && !containsIgnoreCase(name, "vc4");
    });

    append_matches([](const std::string&) { return true; });
    return ordered;
}

SDL_AudioDeviceID openPreferredDevice(int is_capture,
                                      const SDL_AudioSpec& desired,
                                      SDL_AudioSpec* obtained,
                                      std::string* chosen_name) {
    for (const auto& name : candidateDeviceNames(is_capture)) {
        SDL_AudioDeviceID device = SDL_OpenAudioDevice(name.c_str(), is_capture, &desired, obtained,
                                                       SDL_AUDIO_ALLOW_ANY_CHANGE);
        if (device != 0) {
            if (chosen_name != nullptr) {
                *chosen_name = name;
            }
            return device;
        }
    }

    SDL_AudioDeviceID device = SDL_OpenAudioDevice(nullptr, is_capture, &desired, obtained,
                                                   SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (device != 0 && chosen_name != nullptr) {
        *chosen_name = "<default>";
    }
    return device;
}

}  // namespace

AudioPipelineSdl::~AudioPipelineSdl() {
    stopCapture();
    closeDebugCapture();
    stopExternalCapture();

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
    if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            std::cerr << "[audio-sdl] SDL_InitSubSystem(SDL_INIT_AUDIO) failed: " << SDL_GetError() << std::endl;
            return false;
        }
    }

    std::cout << "[audio-sdl] driver=" << (SDL_GetCurrentAudioDriver() != nullptr ? SDL_GetCurrentAudioDriver() : "<none>")
              << std::endl;

    SDL_AudioSpec desired{};
    desired.freq = kTargetRate;
    desired.format = AUDIO_S16SYS;
    desired.samples = 1024;
    desired.callback = nullptr;

    external_capture_ = useExternalCapture();

    // ── capture ───────────────────────────────────────────────────
    if (!external_capture_) {
        // Try default device first (works with correctly configured
        // ALSA, PulseAudio, or PipeWire).  Fall back to enumerating
        // capture devices if the default fails.
        desired.channels = 2;
        std::string capture_name;
        capture_device_ = openPreferredDevice(1, desired, &capture_spec_, &capture_name);
        if (capture_device_ == 0) {
            std::cerr << "[audio-sdl] capture open failed: " << SDL_GetError() << std::endl;
            return false;
        }
        std::cout << "[audio-sdl] using capture device: " << capture_name << std::endl;
    } else {
        capture_spec_.freq = kTargetRate;
        capture_spec_.channels = 1;
        std::cout << "[audio-sdl] using external ALSA capture helper" << std::endl;
    }

    // ── playback ──────────────────────────────────────────────────
    desired.channels = 2;
    std::string playback_name;
    playback_device_ = openPreferredDevice(0, desired, &playback_spec_, &playback_name);
    if (playback_device_ == 0) {
        std::cerr << "[audio-sdl] playback open failed: " << SDL_GetError() << std::endl;
        if (capture_device_ != 0) {
            SDL_CloseAudioDevice(capture_device_);
            capture_device_ = 0;
        }
        return false;
    }
    std::cout << "[audio-sdl] using playback device: " << playback_name << std::endl;

    SDL_PauseAudioDevice(playback_device_, 0);

    std::cout << "[audio-sdl] initialized capture=" << capture_spec_.freq << "Hz/" << static_cast<int>(capture_spec_.channels)
              << "ch playback=" << playback_spec_.freq << "Hz/" << static_cast<int>(playback_spec_.channels) << "ch"
              << std::endl;
    return true;
}

void AudioPipelineSdl::startCapture() {
    closeDebugCapture();
    if (external_capture_) {
        if (!startExternalCapture()) {
            return;
        }
    } else {
        if (capture_device_ == 0) {
            return;
        }
        SDL_ClearQueuedAudio(capture_device_);
        SDL_PauseAudioDevice(capture_device_, 0);
    }
    capturing_ = true;
    std::cout << "[audio-sdl] capture started" << std::endl;
}

void AudioPipelineSdl::stopCapture() {
    if (!capturing_) {
        return;
    }

    if (external_capture_) {
        stopExternalCapture();
    } else if (capture_device_ != 0) {
        SDL_PauseAudioDevice(capture_device_, 1);
    }
    capturing_ = false;
    std::cout << "[audio-sdl] capture stopped" << std::endl;
}

bool AudioPipelineSdl::isCapturing() const {
    return capturing_;
}

std::vector<int16_t> AudioPipelineSdl::readPcmFrame() {
    if (!capturing_) {
        return {};
    }

    if (external_capture_) {
        std::vector<int16_t> mono(kMonoSamples, 0);
        const ssize_t want = static_cast<ssize_t>(kMonoBytes);
        const ssize_t read_bytes = ::read(capture_pipe_fd_, mono.data(), static_cast<size_t>(want));
        if (read_bytes < want) {
            return {};
        }
        return mono;
    }

    if (capture_device_ == 0) {
        return {};
    }

    const int cap_channels = static_cast<int>(capture_spec_.channels);
    const Uint32 cap_frame_bytes = static_cast<Uint32>(kMonoBytes * cap_channels);
    const Uint32 queued = SDL_GetQueuedAudioSize(capture_device_);
    if (queued < cap_frame_bytes) {
        return {};
    }

    // Read interleaved multi-channel data, then downmix to mono.
    std::vector<int16_t> raw(kMonoSamples * cap_channels, 0);
    const Uint32 read = SDL_DequeueAudio(capture_device_, raw.data(), cap_frame_bytes);
    if (read < cap_frame_bytes) {
        return {};
    }

    if (cap_channels == 1) {
        return raw;
    }

    std::vector<int16_t> mono(kMonoSamples, 0);
    for (int i = 0; i < kMonoSamples; i++) {
        int sum = 0;
        for (int ch = 0; ch < cap_channels; ch++) {
            sum += raw[i * cap_channels + ch];
        }
        mono[i] = static_cast<int16_t>(sum / cap_channels);
    }
    return mono;
}

void AudioPipelineSdl::playPcmFrame(const std::vector<int16_t>& pcm) {
    if (playback_device_ == 0 || pcm.empty()) {
        return;
    }

    total_tts_frames_++;
    if (total_tts_frames_ <= 3 || total_tts_frames_ % 50 == 0) {
        std::cout << "[audio-sdl] PCM frame #" << total_tts_frames_ << " (" << pcm.size() << " samples)" << std::endl;
    }

    // Save the current TTS utterance until we return to listening.
    if (debug_raw_ == nullptr) {
        debug_raw_ = fopen("/tmp/xiaozhi_tts.pcm", "wb");
        debug_raw_count_ = 0;
    }
    if (debug_raw_ != nullptr && debug_raw_count_ < 16000 * 15) {
        fwrite(pcm.data(), sizeof(int16_t), pcm.size(), debug_raw_);
        debug_raw_count_ += static_cast<int>(pcm.size());
    }

    // If playback is stereo, duplicate mono samples to both channels.
    const int pb_channels = static_cast<int>(playback_spec_.channels);
    if (pb_channels > 1) {
        std::vector<int16_t> stereo;
        stereo.reserve(pcm.size() * static_cast<size_t>(pb_channels));
        for (size_t i = 0; i < pcm.size(); i++) {
            for (int ch = 0; ch < pb_channels; ch++) {
                stereo.push_back(pcm[i]);
            }
        }
        SDL_QueueAudio(playback_device_, stereo.data(), static_cast<Uint32>(stereo.size() * sizeof(int16_t)));
    } else {
        SDL_QueueAudio(playback_device_, pcm.data(), static_cast<Uint32>(pcm.size() * sizeof(int16_t)));
    }

    const Uint32 max_queue = static_cast<Uint32>(kTargetRate * sizeof(int16_t) * pb_channels * 4);
    Uint32 queued = SDL_GetQueuedAudioSize(playback_device_);
    if (queued > max_queue) {
        std::vector<Uint8> discard(queued - max_queue);
        SDL_DequeueAudio(playback_device_, discard.data(), queued - max_queue);
    }
}

bool AudioPipelineSdl::hasPlaybackDevice() const {
    return playback_device_ != 0;
}

void AudioPipelineSdl::closeDebugCapture() {
    if (debug_raw_ == nullptr) {
        return;
    }

    fclose(debug_raw_);
    debug_raw_ = nullptr;
    std::cout << "[audio-sdl] debug PCM saved: " << debug_raw_count_ << " samples" << std::endl;
}

bool AudioPipelineSdl::useExternalCapture() const {
    const char* value = std::getenv("XIAOZHI_CAPTURE_BACKEND");
    return value != nullptr && std::string(value) == "alsa";
}

bool AudioPipelineSdl::startExternalCapture() {
    stopExternalCapture();

    int pipe_fd[2];
    if (pipe(pipe_fd) != 0) {
        std::perror("[audio-sdl] pipe");
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        std::perror("[audio-sdl] fork");
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return false;
    }

    if (pid == 0) {
        dup2(pipe_fd[1], STDOUT_FILENO);
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        execlp("arecord", "arecord",
               "-D", "plughw:CARD=ES8388Audio,DEV=0",
               "-q",
               "-f", "S16_LE",
               "-r", "16000",
               "-c", "1",
               "-t", "raw",
               static_cast<char*>(nullptr));
        _exit(127);
    }

    close(pipe_fd[1]);
    capture_pipe_fd_ = pipe_fd[0];
    capture_pid_ = pid;
    return true;
}

void AudioPipelineSdl::stopExternalCapture() {
    if (capture_pipe_fd_ >= 0) {
        close(capture_pipe_fd_);
        capture_pipe_fd_ = -1;
    }
    if (capture_pid_ > 0) {
        kill(capture_pid_, SIGTERM);
        waitpid(capture_pid_, nullptr, 0);
        capture_pid_ = -1;
    }
}

}  // namespace xiaozhi
