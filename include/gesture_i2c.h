#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "gesture_protocol.h"
#include "sequence_presentation.h"

class GestureI2c {
public:
    bool begin();
    void emit(GestureProtocol::Direction direction, uint32_t now);
    void cue(uint32_t now);
    void sequence(const GestureSequence::Result &result, uint32_t now);
    void resetDisplay();
    void update(GestureProtocol::State state, GestureProtocol::Error error,
                uint8_t flags, uint32_t failures, uint32_t now);
    void printStatus() const;
    bool acknowledged() const { return attempted_ && lastResult_ == 0; }
private:
    // The bundled camera driver uses I2C controller 1 for SCCB.
    TwoWire bus_{0};
    GestureProtocol::Frame frame_;
    bool ready_ = false, attempted_ = false, dirty_ = true, sequenceMode_ = false;
    uint8_t lastResult_ = 255;
    uint32_t lastAttempt_ = 0, sent_ = 0, failed_ = 0;
};
