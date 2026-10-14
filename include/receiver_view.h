#pragma once
#include "gesture_protocol.h"

// No graphics dependency: freshness, legacy fallback and countdown behavior can
// be tested on the host. A countdown reaching zero never invents a completion.
class ReceiverView {
public:
    GestureProtocol::Frame frame;
    bool connected = false, timedOut = false;
    void received(const GestureProtocol::Frame &incoming, bool event, uint32_t now) {
        using namespace GestureProtocol;
        Frame next = incoming;
        if (next.version == 1) {
            if (event) {
                legacyEventAt_ = now;
                next.phase = Phase::Tracking; next.count = 1; next.parts[0] = next.direction;
            } else if (connected && frame.version == 1 && frame.bootId == next.bootId &&
                       now - legacyEventAt_ < 1200) {
                next.phase = frame.phase; next.count = frame.count;
                next.parts[0] = frame.parts[0];
            }
        }
        frame = next; receivedAt_ = now;
        connected = true; timedOut = false;
    }
    void disconnected() { connected = false; timedOut = true; frame = {}; }
    uint16_t remaining(uint32_t now) const {
        return GestureProtocol::remainingAfter(frame.remainingMs, now - receivedAt_);
    }
    GestureProtocol::Phase phase(uint32_t now) const {
        using namespace GestureProtocol;
        if (!connected) return Phase::None;
        if (frame.version == 1 && now - legacyEventAt_ >= 1200) return Phase::None;
        if (frame.phase == Phase::Cue && (!remaining(now) || frame.uptimeMs - frame.eventMs >= 1000)) return Phase::None;
        return frame.phase;
    }
    const char *title(uint32_t now) const {
        using namespace GestureProtocol;
        if (!connected) return timedOut ? "NO SIGNAL" : "WAITING";
        if (frame.error != Error::None || frame.state == State::Fault) return "ERROR";
        if (frame.state == State::Paused) return "PAUSED";
        if (frame.state == State::Calibrating) return "CALIBRATING";
        if (frame.state == State::Starting) return "STARTING";
        if (frame.version == 1) return "LEGACY";
        switch (phase(now)) {
            case Phase::Cue: return "START";
            case Phase::Complete: return "DONE";
            case Phase::Cancel: return "CANCEL";
            case Phase::Tracking: return "TRACKING";
            case Phase::Waiting: return "WAIT";
            default: return frame.state == State::Tracking ? "TRACKING" : frame.state == State::WaitClear ? "WAIT" : "READY";
        }
    }
private:
    uint32_t receivedAt_ = 0, legacyEventAt_ = 0;
};
