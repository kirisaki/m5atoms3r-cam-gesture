#pragma once
#include <cstdint>

#include "shortcut_map.h"

// Transport supplies ready() and send(const uint8_t (&)[8]). No event queue:
// disconnected/busy events are dropped, and uncertain sends are always released.
template <class Transport>
class HidShortcut {
public:
    explicit HidShortcut(Transport &transport) : transport_(transport) {}
    static constexpr uint32_t LeaseMs = 2000;
    void configure(ShortcutBinding left, ShortcutBinding right, bool enabled, uint32_t now) {
        ShortcutMap map;
        map.keys[1] = left; map.keys[2] = right;
        configure(map, enabled, now);
    }
    void configure(const ShortcutMap &map, bool enabled, uint32_t now) {
        ++generation_;
        // Release the previous mapping before atomically replacing all bindings.
        setEnabled(false, now);
        map_ = map;
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
        ++generation_;
        managed_ = true;
        leaseActive_ = false;
        setEnabled(false, now);
    }
    void standalone(uint32_t now) {
        setEnabled(false, now);
        ++generation_;
        map_ = ShortcutMap::defaults();
        managed_ = leaseActive_ = false;
        setEnabled(true, now);
    }
    bool managed() const { return managed_; }
    bool leaseActive() const { return leaseActive_; }
    uint32_t generation() const { return generation_; }
    const ShortcutMap &bindings() const { return map_; }
    ShortcutBinding binding(bool right) const { return map_.keys[right ? 2 : 1]; }
    bool trigger(bool right, uint32_t now) {
        return triggerPath(right ? 2 : 1, now, generation_);
    }
    bool triggerPath(uint8_t path, uint32_t now, uint32_t generation) {
        poll(now);
        if (generation != generation_ || !ShortcutMap::validPath(path)) return false;
        if (!enabled_ || holding_ || releaseNeeded_ || !transport_.ready()) return false;
        const auto key = map_.keys[path];
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
            ++generation_;
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
        if (enabled_ != enabled) ++generation_;
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
    ShortcutMap map_ = ShortcutMap::defaults();
    uint32_t generation_ = 0;
    uint32_t refreshedAt_ = 0;
    uint32_t pressedAt_ = 0;
};
