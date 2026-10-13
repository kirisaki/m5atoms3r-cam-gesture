#pragma once
#include <M5Unified.h>
#include "gesture_protocol.h"

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
        dirty_ = dirty_ || !connected_ || localError_ || state_ != frame.state || error_ != frame.error;
        connected_ = true;
        timedOut_ = false;
        localError_ = nullptr;
        state_ = frame.state;
        error_ = frame.error;
        if (event) {
            direction_ = frame.direction;
            lastEvent_ = now;
            ++count_;
            bright_ = true;
            dirty_ = true;
        }
    }
    void disconnected() { connected_ = false; timedOut_ = true; dirty_ = true; }
    void fail(const char *message) { localError_ = message; dirty_ = true; }
    void draw(uint32_t now) {
        if (bright_ && now - lastEvent_ >= 1200) { bright_ = false; dirty_ = true; }
        if (!ready_ || !dirty_) return;
        // Bound display traffic while preserving the most recent state/event.
        if (drawn_ && now - lastDraw_ < 100) return;
        drawn_ = true; dirty_ = false; lastDraw_ = now;
        const bool fault = localError_ || (connected_ && error_ != GestureProtocol::Error::None);
        canvas_.fillSprite(Black);
        const uint16_t header = fault || timedOut_ ? Red : connected_ ? Green : Gray;
        canvas_.fillRect(0, 0, 128, 18, header);
        centered(fault ? "ERROR" : connected_ ? "CONNECTED" : timedOut_ ? "NO SIGNAL" : "WAITING", 5, 1, Black);
        const uint16_t foreground = bright_ ? Green : Gray;
        if (fault) {
            centered("ERROR", 48, 3, Red);
        } else if (!connected_ || direction_ == GestureProtocol::Direction::None) {
            const char *idle = !connected_ ? "WAIT" : state_ == GestureProtocol::State::Paused ? "PAUSED" :
                               state_ == GestureProtocol::State::Ready ? "READY" : "WAIT";
            centered(idle, 48, 3, Gray);
        } else {
            arrow(direction_, foreground);
            centered(GestureProtocol::directionName(direction_), 79, 2, foreground);
        }
        const char *status = localError_ ? localError_ : !connected_ ? "Waiting for camera" : errorName();
        centered(status, 104, 1, fault ? Red : White);
        char count[24];
        snprintf(count, sizeof(count), "Count: %lu", static_cast<unsigned long>(count_));
        centered(count, 119, 1, Gray);
        canvas_.pushSprite(0, 0);
    }
private:
    static constexpr uint16_t Black = 0x0000, White = 0xffff, Green = 0x07e0, Red = 0xf800, Gray = 0x9cf3;
    M5Canvas canvas_{&M5.Display};
    bool ready_ = false, dirty_ = true, drawn_ = false, connected_ = false, timedOut_ = false, bright_ = false;
    uint32_t lastDraw_ = 0, lastEvent_ = 0, count_ = 0;
    const char *localError_ = nullptr;
    GestureProtocol::Direction direction_ = GestureProtocol::Direction::None;
    GestureProtocol::State state_ = GestureProtocol::State::Starting;
    GestureProtocol::Error error_ = GestureProtocol::Error::None;
    void centered(const char *text, int y, int size, uint16_t color) {
        canvas_.setTextSize(size);
        canvas_.setTextColor(color);
        canvas_.drawString(text, (128 - canvas_.textWidth(text)) / 2, y);
    }
    const char *errorName() const {
        using namespace GestureProtocol;
        switch (error_) {
            case Error::NoPsram: return "NO PSRAM";
            case Error::CameraInit: return "CAMERA INIT FAILED";
            case Error::Capture: return "CAPTURE FAILED";
            case Error::InvalidFrame: return "INVALID FRAME";
            case Error::StaleFrame: return "CAMERA STALLED";
            case Error::None: break;
        }
        switch (state_) {
            case State::Starting: return "Starting";
            case State::Calibrating: return "Calibrating";
            case State::Ready: return "Ready";
            case State::Tracking: return "Tracking";
            case State::WaitClear: return "Wait for stillness";
            case State::Paused: return "Paused";
            case State::Fault: return "Camera fault";
        }
        return "Unknown";
    }
    void arrow(GestureProtocol::Direction direction, uint16_t color) {
        using GestureProtocol::Direction;
        switch (direction) {
            case Direction::Left:
                canvas_.fillRect(49, 44, 53, 15, color);
                canvas_.fillTriangle(24, 51, 54, 28, 54, 74, color);
                break;
            case Direction::Right:
                canvas_.fillRect(26, 44, 53, 15, color);
                canvas_.fillTriangle(104, 51, 74, 28, 74, 74, color);
                break;
            case Direction::Up:
                canvas_.fillRect(57, 47, 15, 27, color);
                canvas_.fillTriangle(64, 25, 39, 51, 89, 51, color);
                break;
            case Direction::Down:
                canvas_.fillRect(57, 25, 15, 27, color);
                canvas_.fillTriangle(64, 75, 39, 49, 89, 49, color);
                break;
            case Direction::None: break;
        }
    }
};
