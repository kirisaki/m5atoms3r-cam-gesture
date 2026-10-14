#pragma once
#include "gesture_config.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

// Frozen calibration background: a stationary foreground object must not be
// absorbed. A robust brightness fit compensates camera exposure, not geometry.
class BackgroundPresence {
public:
    static constexpr int Width = GestureConfig::Image::Width, Height = GestureConfig::Image::Height;
    static constexpr int Pixels = Width * Height;
    static constexpr int MinX = GestureConfig::Flow::MinX;
    static constexpr int Area = (Width - MinX) * Height;
    static constexpr int Samples = GestureConfig::Detection::CalibrationFrames;
    enum class State { Unknown, Absent, Present };
    struct Result {
        State state = State::Unknown;
        int changed = 0, largest = 0, fitPixels = 0;
        float gain = 1, offset = 0;
    };
    void reset() {
        frames_ = 0;
        std::memset(sum_, 0, sizeof(sum_));
        std::memset(low_, 255, sizeof(low_));
        std::memset(high_, 0, sizeof(high_));
    }
    BackgroundPresence() { reset(); }
    void calibrate(const uint8_t *image) {
        if (frames_ >= Samples) return;
        for (int i = 0; i < Pixels; ++i) {
            sum_[i] += image[i];
            low_[i] = std::min(low_[i], image[i]); high_[i] = std::max(high_[i], image[i]);
        }
        if (++frames_ == Samples) {
            for (int i = 0; i < Pixels; ++i) {
                background_[i] = (sum_[i] + Samples / 2) / Samples;
                tolerance_[i] = std::min(40, std::max(18, 6 + 3 * (high_[i] - low_[i])));
            }
        }
    }
    Result evaluate(const uint8_t *image) {
        Result r;
        if (frames_ < Samples) return r;
        uint16_t histogram[511] = {};
        for (int y = 0; y < Height; ++y)
            for (int x = MinX; x < Width; ++x) {
                const int i = y * Width + x;
                ++histogram[255 + image[i] - background_[i]];
            }
        int count = 0;
        for (int i = 0; i < 511; ++i) {
            count += histogram[i];
            if (count >= (Area + 1) / 2) { r.offset = i - 255; break; }
        }
        // Fit only pixels already close to the background estimate. This keeps
        // a hand from pulling the exposure estimate toward its own brightness.
        for (int pass = 0; pass < 3; ++pass) {
            double sx = 0, sy = 0, sxx = 0, sxy = 0;
            int n = 0;
            for (int y = 0; y < Height; ++y)
                for (int x = MinX; x < Width; ++x) {
                    const int i = y * Width + x;
                    if (std::fabs(image[i] - r.gain * background_[i] - r.offset) > 18) continue;
                    const double a = background_[i], b = image[i];
                    sx += a; sy += b; sxx += a * a; sxy += a * b; ++n;
                }
            r.fitPixels = n;
            if (n < Area / 4) break;
            const double denominator = n * sxx - sx * sx;
            if (denominator < n * n * 25.0) break;
            r.gain = std::max(0.5, std::min(2.0, (n * sxy - sx * sy) / denominator));
            r.offset = (sy - r.gain * sx) / n;
        }
        std::memset(mask_, 0, sizeof(mask_));
        for (int y = 0; y < Height; ++y)
            for (int x = MinX; x < Width; ++x) {
                const int i = y * Width + x;
                if (std::fabs(image[i] - r.gain * background_[i] - r.offset) > tolerance_[i]) {
                    mask_[i] = 1; ++r.changed;
                }
            }
        // Four-connected components suppress isolated exposure/noise residuals.
        for (int y = 0; y < Height; ++y)
            for (int x = MinX; x < Width; ++x) {
                const int start = y * Width + x;
                if (!mask_[start]) continue;
                int head = 0, tail = 0;
                queue_[tail++] = start; mask_[start] = 0;
                while (head < tail) {
                    const int i = queue_[head++], px = i % Width, py = i / Width;
                    if (px > MinX) visit(i - 1, tail);
                    if (px + 1 < Width) visit(i + 1, tail);
                    if (py > 0) visit(i - Width, tail);
                    if (py + 1 < Height) visit(i + Width, tail);
                }
                r.largest = std::max(r.largest, tail);
            }
        // Hysteresis/unknown band: do not call an ambiguous residual empty.
        if (r.largest >= 45) r.state = State::Present;
        else if (r.largest <= 30 && r.changed <= 65 && r.fitPixels * 100 >= Area * 60 &&
                 std::fabs(r.offset) <= 60) r.state = State::Absent;
        return r;
    }
private:
    void visit(int i, int &tail) {
        if (mask_[i]) { mask_[i] = 0; queue_[tail++] = i; }
    }
    int frames_ = 0;
    uint16_t sum_[Pixels] = {}, queue_[Pixels] = {};
    uint8_t low_[Pixels], high_[Pixels], background_[Pixels] = {}, tolerance_[Pixels] = {}, mask_[Pixels] = {};
};
