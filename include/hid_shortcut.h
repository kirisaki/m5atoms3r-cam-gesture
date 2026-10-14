#pragma once
#include <cstdint>

struct ShortcutBinding {
    uint8_t modifiers, usage;
    ShortcutBinding(uint8_t modifiers = 0, uint8_t usage = 0) : modifiers(modifiers), usage(usage) {}
};

// Transport supplies ready() and send(const uint8_t (&)[8]). No event queue:
// disconnected/busy events are dropped, and uncertain sends are always released.
template <class Transport>
class HidShortcut {
public:
    explicit HidShortcut(Transport &transport) : transport_(transport) {}
    static constexpr uint32_t LeaseMs = 2000;
    void configure(ShortcutBinding left, ShortcutBinding right, bool enabled, uint32_t now) {
        // Release the previous mapping before atomically replacing both bindings.
        setEnabled(false, now);
        left_ = left; right_ = right;
        managed_ = leaseActive_ = true;
        refreshedAt_ = now;
        setEnabled(enabled, now);
    }
    bool keepAlive(uint32_t now) {
        poll(now);
        if (!managed_ || !leaseActive_) return false;
        refreshedAt_ = now;
        return true;
    }
    void stop(uint32_t now) {
        managed_ = true;
        leaseActive_ = false;
        setEnabled(false, now);
    }
    void standalone(uint32_t now) {
        setEnabled(false, now);
        left_ = ShortcutBinding(4, 0x50); right_ = ShortcutBinding(4, 0x4f);
        managed_ = leaseActive_ = false;
        setEnabled(true, now);
    }
    bool managed() const { return managed_; }
    bool leaseActive() const { return leaseActive_; }
    ShortcutBinding binding(bool right) const { return right ? right_ : left_; }
    bool trigger(bool right, uint32_t now) {
        poll(now);
        if (!enabled_ || holding_ || releaseNeeded_ || !transport_.ready()) return false;
        const auto key = binding(right);
        if (!key.usage) return false; // An individual direction can be disabled.
        uint8_t report[8] = {key.modifiers, 0, key.usage, 0, 0, 0, 0, 0};
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
        if (managed_ && leaseActive_ && now - refreshedAt_ >= LeaseMs) {
            leaseActive_ = enabled_ = holding_ = false;
        }
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
    bool managed_ = false, leaseActive_ = false;
    ShortcutBinding left_{4, 0x50}, right_{4, 0x4f};
    uint32_t refreshedAt_ = 0;
    uint32_t pressedAt_ = 0;
};
