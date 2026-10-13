#include "gesture_i2c.h"
#include <esp_system.h>

using namespace GestureProtocol;
bool GestureI2c::begin() {
    frame_.bootId = esp_random();
    ready_ = bus_.begin(Sda, Scl, Frequency);
    bus_.setTimeOut(10);
    Serial.printf("# I2C master init=%s addr=0x%02x SDA=%d SCL=%d Hz=%lu\n",
                  ready_ ? "OK" : "FAIL", Address, Sda, Scl, Frequency);
    return ready_;
}
void GestureI2c::emit(Direction direction, uint32_t now) {
    frame_.direction = direction;
    ++frame_.eventSequence;
    frame_.eventMs = now;
    dirty_ = true;
}
void GestureI2c::update(State state, Error error, uint8_t flags, uint32_t failures, uint32_t now) {
    dirty_ = dirty_ || frame_.state != state || frame_.error != error || frame_.flags != flags;
    frame_.state = state; frame_.error = error; frame_.flags = flags; frame_.failures = failures;
    if (!ready_) return;
    // An absent receiver must not add a bus timeout to every camera frame.
    if (attempted_ && now - lastAttempt_ < HeartbeatMs && (!dirty_ || lastResult_ != 0)) return;
    frame_.uptimeMs = now;
    uint8_t packet[PacketSize];
    encode(frame_, packet);
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
    Serial.printf("# I2C init=%d addr=0x%02x wire_result=%u sent=%lu failed=%lu event_seq=%lu\n",
                  ready_, Address, lastResult_, sent_, failed_, frame_.eventSequence);
}
