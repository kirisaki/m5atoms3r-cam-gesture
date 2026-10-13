#pragma once
#include <cstdint>

// Transport supplies ready() and send(const uint8_t (&)[8]). No event queue:
// disconnected/busy events are dropped, and uncertain sends are always released.
template <class Transport>
class HidShortcut {
public:
    explicit HidShortcut(Transport &transport) : transport_(transport) {}
    bool trigger(bool right, uint32_t now) {
        poll(now);
        if (!enabled_ || holding_ || releaseNeeded_ || !transport_.ready()) return false;
        // HID keyboard modifier bit 2 = Left Alt; usages 0x4f/0x50 = right/left.
        uint8_t report[8] = {0x04, 0, static_cast<uint8_t>(right ? 0x4f : 0x50), 0, 0, 0, 0, 0};
        releaseNeeded_ = true;
        holding_ = true;
        pressedAt_ = now;
        if (transport_.send(report)) return true;
        // A timeout can mean the host received the press but its ACK was lost.
        holding_ = false;
        poll(now);
        return false;
    }
    void poll(uint32_t now) {
        if (!transport_.ready()) {
            holding_ = false;
            releaseNeeded_ = true; // Neutral report first after USB reconnect.
            return;
        }
        if (holding_ && enabled_ && now - pressedAt_ < 40) return;
        holding_ = false;
        if (releaseNeeded_) {
            const uint8_t released[8] = {};
            if (transport_.send(released)) releaseNeeded_ = false;
        }
    }
    void setEnabled(bool enabled, uint32_t now) {
        enabled_ = enabled;
        if (!enabled) holding_ = false;
        poll(now);
    }
    bool enabled() const { return enabled_; }
    bool busy() { return holding_ || (releaseNeeded_ && transport_.ready()); }
private:
    Transport &transport_;
    bool enabled_ = true, holding_ = false, releaseNeeded_ = true;
    uint32_t pressedAt_ = 0;
};
