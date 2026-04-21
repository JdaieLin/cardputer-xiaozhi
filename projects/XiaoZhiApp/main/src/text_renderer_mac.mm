#include "text_renderer_mac.hpp"

#include <algorithm>
#include <AppKit/AppKit.h>
#include <cmath>
#include <CoreGraphics/CoreGraphics.h>

#include <vector>

namespace xiaozhi {
namespace {

NSFont* preferredFont(float point_size, bool bold) {
    NSFont* font = [NSFont fontWithName:@"PingFang SC" size:point_size];
    if (font != nil) {
        return font;
    }
    if (@available(macOS 10.11, *)) {
        return [NSFont systemFontOfSize:point_size weight:(bold ? NSFontWeightSemibold : NSFontWeightRegular)];
    }
    return bold ? [NSFont boldSystemFontOfSize:point_size] : [NSFont systemFontOfSize:point_size];
}

}  // namespace

bool drawMacText(SDL_Renderer* renderer, const SDL_Rect& rect, const std::string& text, const TextStyle& style) {
    if (renderer == nullptr || rect.w <= 0 || rect.h <= 0 || text.empty()) {
        return false;
    }

    @autoreleasepool {
        NSString* ns_text = [[NSString alloc] initWithBytes:text.data()
                                                     length:text.size()
                                                   encoding:NSUTF8StringEncoding];
        if (ns_text == nil) {
            return false;
        }

        NSMutableParagraphStyle* paragraph = [[NSMutableParagraphStyle alloc] init];
        paragraph.alignment = style.center ? NSTextAlignmentCenter : NSTextAlignmentLeft;
        paragraph.lineBreakMode = style.wrap ? NSLineBreakByWordWrapping : NSLineBreakByTruncatingTail;

        NSColor* color = [NSColor colorWithSRGBRed:style.color.r / 255.0
                                             green:style.color.g / 255.0
                                              blue:style.color.b / 255.0
                                             alpha:style.color.a / 255.0];

        NSDictionary* attrs = @{
            NSFontAttributeName: preferredFont(style.point_size, style.bold),
            NSForegroundColorAttributeName: color,
            NSParagraphStyleAttributeName: paragraph,
        };

        NSAttributedString* attributed = [[NSAttributedString alloc] initWithString:ns_text attributes:attrs];

        CGColorSpaceRef color_space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGContextRef context = CGBitmapContextCreate(nullptr,
                                                     static_cast<size_t>(rect.w),
                                                     static_cast<size_t>(rect.h),
                                                     8,
                                                     static_cast<size_t>(rect.w) * 4,
                                                     color_space,
                                                     kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst);
        CGColorSpaceRelease(color_space);
        if (context == nullptr) {
            return false;
        }

        CGContextClearRect(context, CGRectMake(0, 0, rect.w, rect.h));

        NSGraphicsContext* graphics = [NSGraphicsContext graphicsContextWithCGContext:context flipped:YES];
        [NSGraphicsContext saveGraphicsState];
        [NSGraphicsContext setCurrentContext:graphics];

        NSUInteger options = NSStringDrawingUsesFontLeading | NSStringDrawingUsesLineFragmentOrigin;
        if (!style.wrap) {
            options |= NSStringDrawingTruncatesLastVisibleLine;
        }

        NSRect draw_rect = NSMakeRect(0, 0, rect.w, rect.h);
        if (!style.wrap) {
            NSRect bounds = [attributed boundingRectWithSize:NSMakeSize(rect.w, CGFLOAT_MAX) options:options];
            const CGFloat text_height = std::ceil(bounds.size.height);
            const CGFloat origin_y = std::max<CGFloat>(0.0, (rect.h - text_height) / 2.0);
            draw_rect = NSMakeRect(0, origin_y, rect.w, text_height);
        }

        [attributed drawWithRect:draw_rect options:options];

        [NSGraphicsContext restoreGraphicsState];

        void* pixels = CGBitmapContextGetData(context);
        if (pixels == nullptr) {
            CGContextRelease(context);
            return false;
        }

        std::vector<unsigned char> flipped_pixels(static_cast<size_t>(rect.w) * static_cast<size_t>(rect.h) * 4);
        const size_t row_bytes = static_cast<size_t>(rect.w) * 4;
        auto* source = static_cast<unsigned char*>(pixels);
        for (int row = 0; row < rect.h; ++row) {
            std::memcpy(flipped_pixels.data() + static_cast<size_t>(row) * row_bytes,
                        source + static_cast<size_t>(rect.h - 1 - row) * row_bytes,
                        row_bytes);
        }

        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(
            flipped_pixels.data(),
            rect.w,
            rect.h,
            32,
            rect.w * 4,
            SDL_PIXELFORMAT_BGRA32);
        if (surface == nullptr) {
            CGContextRelease(context);
            return false;
        }

        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_FreeSurface(surface);
        if (texture == nullptr) {
            CGContextRelease(context);
            return false;
        }

        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_RenderCopy(renderer, texture, nullptr, &rect);
        SDL_DestroyTexture(texture);
        CGContextRelease(context);
        return true;
    }
}

}  // namespace xiaozhi