#pragma once
#include <M5Unified.h>
#include "receiver_view.h"

// Called only by the receiver's main loop, never by the I2C callback.
class ReceiverDisplay {
public:
    void begin() {
        auto config = M5.config();
        config.internal_imu = false;
        config.internal_rtc = false;
        config.internal_mic = false;
        config.internal_spk = false;
        config.external_display_value = 0;
        M5.begin(config);
        M5.Display.setRotation(3); // Rotate the whole screen 90 degrees counterclockwise.
        M5.Display.setBrightness(160);
        canvas_.setColorDepth(16);
        ready_ = canvas_.createSprite(128, 128) != nullptr;
        Serial.printf("# Display board=%u size=%dx%d buffer=%s\n",
                      static_cast<unsigned>(M5.getBoard()), M5.Display.width(), M5.Display.height(),
                      ready_ ? "OK" : "FAIL");
        if (!ready_) {
            M5.Display.fillScreen(Black);
            M5.Display.setTextColor(Red, Black);
            M5.Display.drawString("DISPLAY MEMORY ERROR", 0, 50);
        }
        draw(millis());
    }
    void received(const GestureProtocol::Frame &frame, bool event, uint32_t now) {
        view_.received(frame, event, now); localError_ = nullptr; dirty_ = true;
    }
    void disconnected() { view_.disconnected(); dirty_ = true; }
    void fail(const char *message) { localError_ = message; dirty_ = true; }
    void draw(uint32_t now) {
        using namespace GestureProtocol;
        const auto phase = view_.phase(now);
        const bool animation = view_.frame.phase != Phase::None;
        if (!ready_ || (!dirty_ && !animation)) return;
        if (drawn_ && now - lastDraw_ < 100) return;
        drawn_ = true; dirty_ = false; lastDraw_ = now;
        const bool fault = localError_ || (view_.connected &&
            (view_.frame.error != Error::None || view_.frame.state == State::Fault));
        const auto &f = view_.frame;
        canvas_.fillSprite(Black);
        const uint16_t color = fault || view_.timedOut ? Red : phase == Phase::Cancel ? Orange :
            phase == Phase::Complete || phase == Phase::Cue ? Green : White;
        canvas_.fillCircle(6, 9, 3, view_.connected ? Green : Red);
        centered(fault ? "ERROR" : view_.title(now), 5, 1, color);
        canvas_.drawFastHLine(0, 20, 128, Dark);
        if (fault) {
            centered("ERROR", 49, 3, Red);
            centered(localError_ ? localError_ : errorName(), 96, 1, Red);
        } else if (!view_.connected) {
            centered("WAIT", 49, 3, Gray);
            centered("Waiting for camera", 103, 1, Gray);
        } else if (phase == Phase::Cue) {
            canvas_.fillRect(0, 24, 128, 100, Green);
            centered("START", 54, 3, Black);
        } else {
            const bool showPath = phase == Phase::Tracking || phase == Phase::Complete || phase == Phase::Cancel;
            for (int i = 0; i < 3; ++i) {
                const int x = 6 + i * 40;
                canvas_.drawRoundRect(x, 39, 36, 38, 4, Dark);
                if (showPath && i < f.count) arrow(f.parts[i], x + 18, 58, color);
            }
            const char *bottom = phase == Phase::Cancel ? reasonName(f.reason) :
                f.version == 1 ? "LEGACY" : phase == Phase::Tracking ? "Finish" :
                f.state == State::WaitClear ? "Next input" : "";
            centered(bottom, 90, 1, color);
            if (f.durationMs && (phase == Phase::Tracking || f.state == State::WaitClear)) {
                const uint16_t left = view_.remaining(now);
                canvas_.drawRect(8, 108, 112, 8, Dark);
                const int width = 110 * left / f.durationMs;
                if (width) canvas_.fillRect(9, 109, width, 6, color);
                // Zero means the estimated delay elapsed; READY still comes from camera.
            }
        }
        canvas_.pushSprite(0, 0);
    }
private:
    static constexpr uint16_t Black = 0x0000, White = 0xffff, Green = 0x07e0, Red = 0xf800,
        Gray = 0x9cf3, Dark = 0x4208, Orange = 0xfd20;
    M5Canvas canvas_{&M5.Display};
    ReceiverView view_;
    bool ready_ = false, dirty_ = true, drawn_ = false;
    uint32_t lastDraw_ = 0;
    const char *localError_ = nullptr;
    static const char *reasonName(GestureProtocol::CancelReason reason) {
        using GestureProtocol::CancelReason;
        switch (reason) {
            case CancelReason::Timeout: return "TIMEOUT";
            case CancelReason::TooManySegments: return "TOO MANY STEPS";
            case CancelReason::Global: return "CAMERA MOTION";
            case CancelReason::FrameGap: return "FRAME GAP";
            default: return "CANCELLED";
        }
    }
    void centered(const char *text, int y, int size, uint16_t color) {
        canvas_.setTextSize(size);
        canvas_.setTextColor(color);
        canvas_.drawString(text, (128 - canvas_.textWidth(text)) / 2, y);
    }
    const char *errorName() const {
        using namespace GestureProtocol;
        switch (view_.frame.error) {
            case Error::NoPsram: return "NO PSRAM";
            case Error::CameraInit: return "CAMERA INIT FAILED";
            case Error::Capture: return "CAPTURE FAILED";
            case Error::InvalidFrame: return "INVALID FRAME";
            case Error::StaleFrame: return "CAMERA STALLED";
            case Error::None: break;
        }
        return "Camera fault";
    }
    void arrow(GestureProtocol::Direction direction, int x, int y, uint16_t color) {
        using GestureProtocol::Direction;
        switch (direction) {
            case Direction::Left:
                canvas_.fillRect(x-3, y-3, 16, 7, color);
                canvas_.fillTriangle(x-14,y, x-2,y-11, x-2,y+11, color); break;
            case Direction::Right:
                canvas_.fillRect(x-13, y-3, 16, 7, color);
                canvas_.fillTriangle(x+14,y, x+2,y-11, x+2,y+11, color); break;
            case Direction::Up:
                canvas_.fillRect(x-3, y-3, 7, 16, color);
                canvas_.fillTriangle(x,y-14, x-11,y-2, x+11,y-2, color); break;
            case Direction::Down:
                canvas_.fillRect(x-3, y-13, 7, 16, color);
                canvas_.fillTriangle(x,y+14, x-11,y+2, x+11,y+2, color); break;
            case Direction::None: break;
        }
    }
};
