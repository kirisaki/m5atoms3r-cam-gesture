#pragma once
#include "gesture_config.h"
#include "gesture_protocol.h"
#include <cmath>

// Experimental: collect directions until coherent motion stops briefly.
// A pause inside the image completes too; this does not infer hand presence.
#ifndef GESTURE_SEQUENCE_STOP_MS
#define GESTURE_SEQUENCE_STOP_MS 400
#endif
class GestureSequence {
public:
    static constexpr int MaxSegments = 3;
    using Direction = GestureProtocol::Direction;
    enum class Event { None, Segment, Complete, Cancel };
    enum class Reason { None, Timeout, TooManySegments, Global, FrameGap };
    struct Motion {
        float x = 0, y = 0; // Mounted coordinates: right/down positive.
        int inliers = 0, moving = 0;
        bool global = false;
    };
    struct Result {
        Event event = Event::None;
        Reason reason = Reason::None;
        Direction parts[MaxSegments] = {};
        int count = 0;
        bool tracking = false, waiting = false;
        uint32_t idleMs = 0;
    };
    static constexpr int ContinuationSpeed = 20;
    static constexpr uint32_t StopQuietMs = GESTURE_SEQUENCE_STOP_MS;
    static constexpr uint32_t RearmDelayMs = 1500;
    static constexpr uint32_t MaxDurationMs = 3500;
    void reset() { *this = GestureSequence(); }
    Result update(const Motion &m, uint32_t now, uint32_t dt) {
        using namespace GestureConfig::Detection;
        // Count coherent diagonal motion while rounding a corner as activity,
        // even when it cannot yet be assigned a cardinal direction.
        const float ax = std::fabs(m.x), ay = std::fabs(m.y);
        const float major = ax > ay ? ax : ay;
        const bool active = dt && m.inliers >= 4 && major >= MinFrameMotion &&
                            major * 1000 >= dt * ContinuationSpeed;
        if (dt == 0 || dt > MaxFrameGapMs) return cancel(Reason::FrameGap, now);
        if (m.global) return cancel(Reason::Global, now);
        if (waiting_) {
            if (active) quietSince_ = now;
            if (now - quietSince_ >= QuietMs && now - blockedAt_ >= RearmDelayMs) waiting_ = false;
            return result();
        }
        if (tracking_ && now - startedAt_ > MaxDurationMs) return cancel(Reason::Timeout, now);
        if (active) lastMotion_ = now;
        idleMs_ = tracking_ ? now - lastMotion_ : 0;
        Direction direction = Direction::None;
        if (m.inliers >= 4 && major >= MinFrameMotion) {
            if (ax >= ay * AxisDominance) direction = m.x > 0 ? Direction::Right : Direction::Left;
            else if (ay >= ax * AxisDominance) direction = m.y > 0 ? Direction::Down : Direction::Up;
        }
        const bool fast = direction != Direction::None && active;
        Event event = Event::None;
        if (direction != Direction::None) {
            if (direction != candidate_ || now - candidateAt_ > MotionTimeoutMs) {
                candidate_ = direction; samples_ = 0; travel_ = 0; peakSpeed_ = 0;
            }
            candidateAt_ = now;
            if (samples_ && now - candidateStarted_ > 250) { samples_ = 0; travel_ = peakSpeed_ = 0; }
            if (fast) {
                if (samples_ == 0) candidateStarted_ = now;
                ++samples_; travel_ += major;
                const float speed = major * 1000 / dt;
                if (speed > peakSpeed_) peakSpeed_ = speed;
                // Each leg must be intentional, but slowing while rounding a
                // corner does not erase already confirmed segments.
                if (samples_ >= 2 && now - candidateStarted_ >= FastMinGestureMs &&
                    travel_ >= (tracking_ ? 4.5f : 6.0f) &&
                    peakSpeed_ >= (tracking_ ? ContinuationSpeed : MinSpeedPixelsPerSecond)) {
                    if (!tracking_) { tracking_ = true; startedAt_ = now; lastMotion_ = now; }
                    if (count_ == 0 || parts_[count_ - 1] != direction) {
                        if (count_ == MaxSegments) return cancel(Reason::TooManySegments, now);
                        parts_[count_++] = direction;
                        event = Event::Segment;
                    }
                }
            }
        } else if (now - candidateAt_ > MotionTimeoutMs) {
            candidate_ = Direction::None; samples_ = 0; travel_ = 0;
        }
        if (tracking_ && !active && idleMs_ >= StopQuietMs) {
            Result completed = result(Event::Complete);
            block(now);
            completed.tracking = false; completed.waiting = true;
            return completed;
        }
        return result(event);
    }
    static const char *reasonName(Reason reason) {
        switch (reason) {
            case Reason::Timeout: return "TIMEOUT";
            case Reason::TooManySegments: return "TOO_MANY_SEGMENTS";
            case Reason::Global: return "GLOBAL";
            case Reason::FrameGap: return "FRAME_GAP";
            default: return "NONE";
        }
    }
private:
    Result result(Event event = Event::None, Reason reason = Reason::None) const {
        Result r;
        r.event = event; r.reason = reason; r.count = count_;
        for (int i = 0; i < count_; ++i) r.parts[i] = parts_[i];
        r.tracking = tracking_; r.waiting = waiting_; r.idleMs = idleMs_;
        return r;
    }
    void block(uint32_t now) {
        reset(); waiting_ = true; blockedAt_ = quietSince_ = now;
    }
    Result cancel(Reason reason, uint32_t now) {
        auto r = result(tracking_ ? Event::Cancel : Event::None, reason);
        block(now); r.tracking = false; r.waiting = true;
        return r;
    }
    Direction parts_[MaxSegments] = {};
    Direction candidate_ = Direction::None;
    int count_ = 0, samples_ = 0;
    float travel_ = 0, peakSpeed_ = 0;
    bool tracking_ = false, waiting_ = false;
    uint32_t startedAt_ = 0, lastMotion_ = 0, candidateAt_ = 0, candidateStarted_ = 0;
    uint32_t quietSince_ = 0, blockedAt_ = 0, idleMs_ = 0;
};
