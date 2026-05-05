#pragma once

#include <cstdint>
#include <string>

#include "types.hpp"
#include "ui.hpp"

namespace xiaozhi {

class UiFbdev final : public Ui {
public:
    UiFbdev();
    ~UiFbdev() override;

    bool init() override;
    void renderState(AppState state, const std::string& text, const std::string& emoji) override;

private:
    static constexpr int kWidth = 320;
    static constexpr int kHeight = 170;
    static constexpr int kGlyphW = 5;
    static constexpr int kGlyphH = 7;

    static const uint8_t* glyphFor(char c);

    void setPixel(int x, int y, uint16_t color);
    void fillRect(int x, int y, int w, int h, uint16_t color);
    void drawGlyph(int x, int y, const uint8_t* glyph, int scale, uint16_t color);
    void drawText(int x, int y, const std::string& text, int scale, uint16_t color);

    static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b);
    static uint16_t stateColor(AppState state);
    static const char* stateLabel(AppState state);

    int fb_fd_ = -1;
    uint16_t* fb_map_ = nullptr;
    size_t fb_size_ = 0;
    int line_length_ = 0;
};

}  // namespace xiaozhi
