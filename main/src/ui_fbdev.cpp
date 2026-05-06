#include "ui_fbdev.hpp"

#include <cctype>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/fb.h>

namespace xiaozhi {
namespace {

constexpr int kGlyphW = 5;
constexpr int kGlyphH = 7;

const uint8_t* internalGlyphFor(char c) {
    static const uint8_t kUnknown[kGlyphH] = {0x1f, 0x11, 0x15, 0x15, 0x15, 0x11, 0x1f};
    static const uint8_t kSpace[kGlyphH]    = {0, 0, 0, 0, 0, 0, 0};
    static const uint8_t kColon[kGlyphH]    = {0, 0x04, 0x04, 0, 0x04, 0x04, 0};
    static const uint8_t kDash[kGlyphH]     = {0, 0, 0, 0x1f, 0, 0, 0};
    static const uint8_t kDot[kGlyphH]      = {0, 0, 0, 0, 0, 0x0c, 0x0c};
    static const uint8_t kSlash[kGlyphH]    = {0x01, 0x02, 0x04, 0x08, 0x10, 0, 0};
    static const uint8_t kDigits[10][kGlyphH] = {
        {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e},
        {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e},
        {0x0e, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1f},
        {0x1f, 0x01, 0x06, 0x01, 0x01, 0x11, 0x0e},
        {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02},
        {0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e},
        {0x0e, 0x10, 0x1e, 0x11, 0x11, 0x11, 0x0e},
        {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
        {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e},
        {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x1c},
    };
    static const uint8_t kUpper[26][kGlyphH] = {
        {0x04, 0x0a, 0x11, 0x11, 0x1f, 0x11, 0x11}, {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e},
        {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e}, {0x1c, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1c},
        {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f}, {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10},
        {0x0e, 0x11, 0x10, 0x10, 0x13, 0x11, 0x0e}, {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11},
        {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e}, {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0c},
        {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f},
        {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11}, {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},
        {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}, {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10},
        {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d}, {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11},
        {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e}, {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
        {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}, {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04},
        {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a}, {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11},
        {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04}, {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f},
    };

    if (c == ' ') return kSpace;
    if (c == ':') return kColon;
    if (c == '-') return kDash;
    if (c == '.') return kDot;
    if (c == '/') return kSlash;
    if (c >= '0' && c <= '9') return kDigits[c - '0'];
    if (c >= 'a' && c <= 'z') c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (c >= 'A' && c <= 'Z') return kUpper[c - 'A'];
    return kUnknown;
}

int textWidth(const std::string& text, int scale) {
    return static_cast<int>(text.size()) * (kGlyphW + 1) * scale;
}

}  // namespace

const uint8_t* UiFbdev::glyphFor(char c) {
    return internalGlyphFor(c);
}

UiFbdev::UiFbdev() = default;

UiFbdev::~UiFbdev() {
    if (fb_map_ != nullptr && fb_map_ != MAP_FAILED) {
        munmap(fb_map_, fb_size_);
    }
    if (fb_fd_ >= 0) {
        close(fb_fd_);
    }
}

bool UiFbdev::init() {
    const char* device = std::getenv("XIAOZHI_FBDEV");
    if (device == nullptr) {
        device = "/dev/fb0";
    }

    fb_fd_ = open(device, O_RDWR);
    if (fb_fd_ < 0) {
        std::cerr << "[ui-fbdev] failed to open " << device << ": " << std::strerror(errno) << std::endl;
        return false;
    }

    struct fb_fix_screeninfo fix{};
    if (ioctl(fb_fd_, FBIOGET_FSCREENINFO, &fix) < 0) {
        std::cerr << "[ui-fbdev] FBIOGET_FSCREENINFO failed" << std::endl;
        close(fb_fd_);
        fb_fd_ = -1;
        return false;
    }

    struct fb_var_screeninfo var{};
    if (ioctl(fb_fd_, FBIOGET_VSCREENINFO, &var) < 0) {
        std::cerr << "[ui-fbdev] FBIOGET_VSCREENINFO failed" << std::endl;
        close(fb_fd_);
        fb_fd_ = -1;
        return false;
    }

    fb_size_ = fix.smem_len;
    line_length_ = fix.line_length;

    fb_map_ = static_cast<uint16_t*>(mmap(nullptr, fb_size_, PROT_READ | PROT_WRITE,
                                           MAP_SHARED, fb_fd_, 0));
    if (fb_map_ == MAP_FAILED) {
        std::cerr << "[ui-fbdev] mmap failed" << std::endl;
        close(fb_fd_);
        fb_fd_ = -1;
        return false;
    }

    std::cout << "[ui-fbdev] initialized " << var.xres << "x" << var.yres
              << " bpp=" << var.bits_per_pixel << " line_len=" << line_length_ << std::endl;
    return true;
}

void UiFbdev::renderState(AppState state, const std::string& text, const std::string& emoji) {
    if (fb_map_ == nullptr || fb_map_ == MAP_FAILED) {
        return;
    }

    const uint16_t bg = rgb565(10, 10, 15);
    const uint16_t header_bg = stateColor(state);
    const uint16_t white = rgb565(255, 255, 255);
    const uint16_t dim = rgb565(140, 140, 160);
    const uint16_t code_color = rgb565(255, 244, 180);
    const uint16_t accent = rgb565(255, 200, 60);

    fillRect(0, 0, kWidth, kHeight, bg);

    fillRect(0, 0, kWidth, 34, header_bg);

    drawText(12, 8, "XIAOZHI", 2, white);

    std::string emoji_str;
    if (!emoji.empty()) {
        emoji_str = emoji;
    }
    drawEmoji(kWidth - 42, 5, 2, state, emoji_str);

    const char* label = stateLabel(state);
    drawText(12, 42, label, 2, white);

    if (state == AppState::Binding) {
        drawText(16, 68, "Open XiaoZhi app and bind", 1, dim);
        std::string code_str;
        for (char c : text) {
            if (c >= '0' && c <= '9') code_str.push_back(c);
        }
        if (code_str.size() == 6) {
            int cw = textWidth("000000", 4);
            drawText((kWidth - cw) / 2, 96, code_str, 4, code_color);
        }
    } else if (state == AppState::Idle) {
        drawText(16, 68, "SPACE / ENTER TO WAKE", 1, dim);
    } else if (state == AppState::Listening) {
        drawText(16, 68, "Listening...", 1, dim);
    } else if (state == AppState::Thinking) {
        drawText(16, 68, "Thinking...", 1, dim);
    } else if (state == AppState::Speaking) {
        drawText(16, 68, "Speaking...", 1, dim);
    } else if (state == AppState::Error) {
        drawText(16, 68, "ERROR", 1, rgb565(255, 80, 80));
    }

    fillRect(0, kHeight - 36, kWidth, 36, rgb565(30, 30, 40));
    drawText(6, kHeight - 28, text, 1, white);
}

void UiFbdev::setPixel(int x, int y, uint16_t color) {
    if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) return;
    fb_map_[y * (line_length_ / 2) + x] = color;
}

void UiFbdev::fillRect(int x, int y, int w, int h, uint16_t color) {
    if (x >= kWidth || y >= kHeight) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > kWidth) w = kWidth - x;
    if (y + h > kHeight) h = kHeight - y;
    if (w <= 0 || h <= 0) return;

    const int stride = line_length_ / 2;
    for (int row = 0; row < h; ++row) {
        uint16_t* dest = fb_map_ + (y + row) * stride + x;
        for (int col = 0; col < w; ++col) {
            dest[col] = color;
        }
    }
}

void UiFbdev::drawGlyph(int x, int y, const uint8_t* glyph, int scale, uint16_t color) {
    for (int row = 0; row < kGlyphH; ++row) {
        for (int col = 0; col < kGlyphW; ++col) {
            if (((glyph[row] >> (kGlyphW - 1 - col)) & 0x01) == 0) continue;
            for (int sy = 0; sy < scale; ++sy) {
                for (int sx = 0; sx < scale; ++sx) {
                    setPixel(x + col * scale + sx, y + row * scale + sy, color);
                }
            }
        }
    }
}

void UiFbdev::drawText(int x, int y, const std::string& text, int scale, uint16_t color) {
    const int advance = (kGlyphW + 1) * scale;
    int cx = x;
    for (char c : text) {
        if (c == '\n') {
            cx = x;
            y += (kGlyphH + 2) * scale;
            continue;
        }
        drawGlyph(cx, y, internalGlyphFor(c), scale, color);
        cx += advance;
        if (cx + advance > kWidth) break;
    }
}

namespace {

struct EmojiIcon {
    int w;
    int h;
    const uint8_t* data;
};

const uint8_t kIconBind[] = {
    0,0,0,0,1,1,1,0,0,0,0,
    0,0,0,1,0,0,0,1,0,0,0,
    0,0,1,0,0,0,0,0,1,0,0,
    0,1,0,0,0,0,0,0,0,1,0,
    1,0,0,0,0,0,0,0,0,0,1,
    1,0,0,0,0,0,0,0,0,0,1,
    1,0,0,0,1,1,0,0,0,0,1,
    0,1,0,0,1,1,0,0,0,1,0,
    0,0,1,0,0,0,0,0,1,0,0,
    0,0,0,1,0,0,0,1,0,0,0,
    0,0,0,0,1,1,1,0,0,0,0,
};

const uint8_t kIconSmile[] = {
    0,0,0,1,1,1,1,1,0,0,0,
    0,0,1,0,0,0,0,0,1,0,0,
    0,1,0,0,0,0,0,0,0,1,0,
    1,0,1,0,0,0,0,1,0,0,1,
    1,0,0,0,0,0,0,0,0,0,1,
    1,0,0,0,0,0,0,0,0,0,1,
    1,0,1,0,0,0,0,1,0,0,1,
    0,1,0,0,0,0,0,0,0,1,0,
    0,0,1,0,0,0,0,0,1,0,0,
    0,0,0,1,0,1,0,1,0,0,0,
    0,0,0,0,1,1,1,0,0,0,0,
};

const uint8_t kIconMic[] = {
    0,0,0,1,1,0,1,1,0,0,0,
    0,0,0,1,1,0,1,1,0,0,0,
    0,0,0,1,0,1,0,1,0,0,0,
    0,0,0,1,0,1,0,1,0,0,0,
    0,0,0,1,0,0,0,1,0,0,0,
    0,0,1,1,1,1,1,1,1,0,0,
    0,1,0,1,1,1,1,1,0,1,0,
    0,0,0,0,1,1,1,0,0,0,0,
    0,0,0,0,0,1,0,0,0,0,0,
    0,0,0,0,0,1,0,0,0,0,0,
    0,0,0,0,0,1,0,0,0,0,0,
};

const uint8_t kIconThink[] = {
    0,0,1,1,1,1,1,1,0,0,0,
    0,1,0,0,0,0,0,0,1,0,0,
    1,0,0,0,0,0,0,0,0,1,0,
    1,0,0,0,0,0,1,0,0,1,0,
    1,0,0,0,0,1,0,0,0,1,0,
    1,0,0,0,1,0,0,0,0,1,0,
    1,0,0,1,0,0,0,0,0,1,0,
    1,0,0,0,0,0,0,0,0,1,0,
    0,1,0,0,0,0,0,0,1,0,0,
    0,0,1,0,0,0,0,1,0,0,0,
    0,0,0,1,0,0,1,0,0,0,0,
};

const uint8_t kIconSpeak[] = {
    0,0,0,0,1,0,0,0,0,0,0,
    0,0,0,1,1,0,0,0,0,0,0,
    0,0,0,1,1,1,0,0,0,1,0,
    0,0,0,1,1,1,1,0,1,0,0,
    1,1,1,1,1,1,1,1,0,0,0,
    1,1,1,1,1,1,1,1,0,0,0,
    0,0,0,1,1,1,1,0,1,0,0,
    0,0,0,1,1,1,0,0,0,1,0,
    0,0,0,1,1,0,0,0,0,0,0,
    0,0,0,0,1,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,
};

const uint8_t kIconError[] = {
    1,0,0,0,0,0,0,0,0,0,1,
    0,1,0,0,0,0,0,0,0,1,0,
    0,0,1,0,0,0,0,0,1,0,0,
    0,0,0,1,0,0,0,1,0,0,0,
    0,0,0,0,1,0,1,0,0,0,0,
    0,0,0,0,0,1,0,0,0,0,0,
    0,0,0,0,1,0,1,0,0,0,0,
    0,0,0,1,0,0,0,1,0,0,0,
    0,0,1,0,0,0,0,0,1,0,0,
    0,1,0,0,0,0,0,0,0,1,0,
    1,0,0,0,0,0,0,0,0,0,1,
};

const EmojiIcon kIcons[] = {
    {11, 11, kIconBind},     // AppState::Binding
    {11, 11, kIconSmile},   // AppState::Idle
    {11, 11, kIconMic},     // AppState::Listening
    {11, 11, kIconThink},   // AppState::Thinking
    {11, 11, kIconSpeak},   // AppState::Speaking
    {11, 11, kIconError},   // AppState::Error
};

}  // namespace

void UiFbdev::drawEmoji(int x, int y, int scale, AppState state, const std::string& emoji) {
    uint16_t color;
    switch (state) {
        case AppState::Binding:   color = rgb565(60, 180, 255); break;
        case AppState::Idle:      color = rgb565(100, 220, 100); break;
        case AppState::Listening: color = rgb565(255, 60, 60); break;
        case AppState::Thinking:  color = rgb565(255, 220, 40); break;
        case AppState::Speaking:  color = rgb565(220, 80, 180); break;
        case AppState::Error:     color = rgb565(255, 50, 50); break;
    }

    int icon_idx = static_cast<int>(state);
    if (icon_idx < 0 || icon_idx >= 6) icon_idx = 1;

    const auto& icon = kIcons[icon_idx];

    for (int row = 0; row < icon.h; ++row) {
        for (int col = 0; col < icon.w; ++col) {
            if (icon.data[row * icon.w + col] == 0) continue;
            for (int sy = 0; sy < scale; ++sy) {
                for (int sx = 0; sx < scale; ++sx) {
                    setPixel(x + col * scale + sx, y + row * scale + sy, color);
                }
            }
        }
    }
}

uint16_t UiFbdev::rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

uint16_t UiFbdev::stateColor(AppState state) {
    switch (state) {
        case AppState::Binding:    return rgb565(30, 50, 95);
        case AppState::Idle:       return rgb565(35, 80, 35);
        case AppState::Listening:  return rgb565(25, 85, 130);
        case AppState::Thinking:   return rgb565(120, 100, 20);
        case AppState::Speaking:   return rgb565(110, 40, 90);
        case AppState::Error:      return rgb565(130, 25, 25);
    }
    return rgb565(30, 86, 145);
}

const char* UiFbdev::stateLabel(AppState state) {
    switch (state) {
        case AppState::Binding:    return "BINDING";
        case AppState::Idle:       return "IDLE";
        case AppState::Listening:  return "LISTENING...";
        case AppState::Thinking:   return "THINKING...";
        case AppState::Speaking:   return "SPEAKING...";
        case AppState::Error:      return "ERROR";
    }
    return "UNKNOWN";
}

}  // namespace xiaozhi
