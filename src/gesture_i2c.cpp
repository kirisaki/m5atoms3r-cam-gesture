#include "gesture_i2c.h"
#include <esp_system.h>

using namespace GestureProtocol;
bool GestureI2c::begin() {
    frame_.bootId = esp_random();
    frame_.version = 2;
    ready_ = bus_.begin(Sda, Scl, Frequency);
    bus_.setTimeOut(10);
    Serial.printf("# I2C master init=%s addr=0x%02x SDA=%d SCL=%d Hz=%lu\n",
                  ready_ ? "OK" : "FAIL", Address, Sda, Scl, Frequency);
    return ready_;
}
void GestureI2c::emit(Direction direction, uint32_t now) {
    frame_.direction = direction;
    frame_.phase = Phase::Complete; frame_.reason = CancelReason::None;
    frame_.count = 1; frame_.parts[0] = direction;
    frame_.parts[1] = frame_.parts[2] = Direction::None;
    frame_.remainingMs = frame_.durationMs = 1200;
    ++frame_.eventSequence;
    frame_.eventMs = now;
    dirty_ = true;
}
void GestureI2c::cue(uint32_t now) {
    resetDisplay();
    frame_.phase = Phase::Cue; frame_.eventMs = now;
    frame_.remainingMs = frame_.durationMs = 1000;
    ++frame_.eventSequence; dirty_ = true;
}
void GestureI2c::resetDisplay() {
    frame_.phase = Phase::None; frame_.reason = CancelReason::None; frame_.count = 0;
    frame_.direction = Direction::None;
    for (auto &part : frame_.parts) part = Direction::None;
    frame_.remainingMs = frame_.durationMs = 0; dirty_ = true;
}
void GestureI2c::sequence(const GestureSequence::Result &r, uint32_t now) {
    sequenceMode_ = true;
    const Phase oldPhase = frame_.phase;
    presentSequence(frame_, r, now);
    dirty_ = dirty_ || oldPhase != frame_.phase || r.event != GestureSequence::Event::None;
}
void GestureI2c::update(State state, Error error, uint8_t flags, uint32_t failures, uint32_t now) {
    dirty_ = dirty_ || frame_.state != state || frame_.error != error || frame_.flags != flags;
    frame_.state = state; frame_.error = error; frame_.flags = flags; frame_.failures = failures;
    if (frame_.phase == Phase::Cue || (!sequenceMode_ && frame_.phase == Phase::Complete)) {
        frame_.remainingMs = remainingAfter(frame_.durationMs, now - frame_.eventMs);
        if (!frame_.remainingMs) resetDisplay();
    }
    if (!ready_) return;
    // An absent receiver must not add a bus timeout to every camera frame.
    if (attempted_ && ((lastResult_ != 0 && now - lastAttempt_ < HeartbeatMs) ||
        (lastResult_ == 0 && !dirty_ && now - lastAttempt_ < (frame_.phase == Phase::None ? HeartbeatMs : DisplayUpdateMs)))) return;
    frame_.uptimeMs = now;
    uint8_t packet[DisplayPacketSize];
    encodeDisplay(frame_, packet);
    bus_.beginTransmission(Address);
    bus_.write(packet, sizeof(packet));
    const uint8_t result = bus_.endTransmission();
    if (!attempted_ || result != lastResult_)
        Serial.printf("# I2C link=%s wire_result=%u\n", result == 0 ? "ACK" : "FAIL", result);
    lastResult_ = result; lastAttempt_ = now; attempted_ = true;
    if (result == 0) { ++sent_; dirty_ = false; }
    else ++failed_;
}
void GestureI2c::printStatus() const {
    Serial.printf("# I2C protocol=2 packet_bytes=%u\n", static_cast<unsigned>(DisplayPacketSize));
    Serial.printf("# I2C init=%d addr=0x%02x wire_result=%u sent=%lu failed=%lu event_seq=%lu\n",
                  ready_, Address, lastResult_, sent_, failed_, frame_.eventSequence);
}
