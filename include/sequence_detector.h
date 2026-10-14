#pragma once
#include "gesture_detector.h"
#include "gesture_sequence.h"

class SequenceDetector {
public:
    using State = GestureDetector::State;
    static constexpr int CalibrationFrames = GestureDetector::CalibrationFrames;
    struct Result : GestureDetector::Result {
        GestureSequence::Result sequence;
        float centerX = 0, centerY = 0;
        int edgeVotes = 0;
    };
    void setVerticalAxis(bool vertical) { vertical_ = vertical; reset(); }
    void setMirror(bool mirror) { mirror_ = mirror; reset(); }
    int calibrationFrames() const { return calibration_; }
    void reset() { calibration_ = 0; sequence_.reset(); }
    Result process(const uint8_t *image, uint32_t now) {
        Result r;
        const uint32_t dt = now - lastFrame_;
        lastFrame_ = now;
        if (calibration_ < CalibrationFrames) {
            ++calibration_;
            std::memcpy(previous_, image, sizeof(previous_));
            if (calibration_ == CalibrationFrames) r.state = State::Ready;
            return r;
        }
        OpticalFlow::Result f;
        if (dt && dt <= GestureConfig::Detection::MaxFrameGapMs) f = flow_.estimate2D(previous_, image);
        std::memcpy(previous_, image, sizeof(previous_));
        const float along = vertical_ ? f.y * OpticalFlow::Width / OpticalFlow::Height : f.x;
        const float across = vertical_ ? f.x * OpticalFlow::Height / OpticalFlow::Width : f.y;
        const float angle = GestureConfig::Detection::AxisTiltDegrees * 0.01745329252f;
        // Same horizontal mounting correction as the single-swipe detector.
        // Physical downward motion in the first labeled trial was raw +x.
        r.alongFlow = (along * std::cos(angle) + across * std::sin(angle)) * (mirror_ ? -1 : 1);
        r.acrossFlow = (across * std::cos(angle) - along * std::sin(angle));
        r.inliers = f.inliers; r.movingPatches = f.moving; r.globalMotion = f.global;
        r.centerX = f.centerX; r.centerY = f.centerY; r.edgeVotes = f.edgeVotes;
        GestureSequence::Motion m;
        m.x = r.alongFlow; m.y = r.acrossFlow;
        m.inliers = f.inliers; m.moving = f.moving; m.global = f.global;
        r.sequence = sequence_.update(m, now, dt);
        r.state = r.sequence.waiting ? State::WaitClear : r.sequence.tracking ? State::Tracking : State::Ready;
        return r;
    }
private:
    OpticalFlow flow_;
    GestureSequence sequence_;
    uint8_t previous_[GestureDetector::Pixels] = {};
    uint32_t lastFrame_ = 0;
    int calibration_ = 0;
    bool vertical_ = true, mirror_ = true;
};
