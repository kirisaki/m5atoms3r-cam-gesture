#pragma once
#include "gesture_sequence.h"

// Keep the completed/cancelled path while the detector itself is rearming.
inline void presentSequence(GestureProtocol::Frame &frame, const GestureSequence::Result &r, uint32_t now) {
    using namespace GestureProtocol;
    using Event = GestureSequence::Event;
    frame.version = 2;
    if (frame.phase == Phase::Cue && r.event == Event::None && now - frame.eventMs < 1000) return;
    if (r.event != Event::None) {
        ++frame.eventSequence; frame.eventMs = now;
        frame.count = static_cast<uint8_t>(r.count);
        for (int i = 0; i < 3; ++i) frame.parts[i] = i < r.count ? r.parts[i] : Direction::None;
        frame.direction = r.count ? r.parts[r.count - 1] : Direction::None;
        frame.reason = r.reason;
    }
    if (r.event == Event::Complete) frame.phase = Phase::Complete;
    else if (r.event == Event::Cancel) frame.phase = Phase::Cancel;
    else if (r.tracking) frame.phase = Phase::Tracking;
    else if (r.waiting) {
        if (frame.phase != Phase::Complete && frame.phase != Phase::Cancel) {
            frame.phase = Phase::Waiting; frame.count = 0;
            for (auto &part : frame.parts) part = Direction::None;
            frame.reason = CancelReason::None;
        }
    } else {
        frame.phase = Phase::None; frame.count = 0; frame.reason = CancelReason::None;
        for (auto &part : frame.parts) part = Direction::None;
    }
    frame.durationMs = r.tracking ? GestureSequence::StopQuietMs : r.waiting ? GestureSequence::RearmDelayMs : 0;
    frame.remainingMs = r.tracking ? remainingAfter(GestureSequence::StopQuietMs, r.idleMs) : r.remainingMs;
}
