#pragma once
#include "optical_flow.h"
#include <cstring>

class GestureDetector {
public:
    static constexpr int Width = OpticalFlow::Width, Height = OpticalFlow::Height;
    static constexpr int Pixels = Width * Height;
    static constexpr int CalibrationFrames = GestureConfig::Detection::CalibrationFrames;
    enum class Direction { None, Left, Right };
    enum class State { Calibrating, Ready, Tracking, WaitClear };
    struct Result {
        Direction direction = Direction::None;
        State state = State::Calibrating;
        int inliers = 0;       // Consistent moving patches.
        int movingPatches = 0;  // All candidate moving patches.
        // Frame-to-frame flow along/across the selected axis.
        float alongFlow = 0, acrossFlow = 0;
        float travel = 0;     // Accumulated flow along the selected axis.
        bool globalMotion = false;
    };
    void reset() {
        calibration_ = 0;
        state_ = State::Calibrating;
        haveTime_ = clearing_ = false;
        travel_ = path_ = cross_ = 0;
        samples_ = 0;
        fastSamples_ = 0;
        fastTravel_ = 0;
    }
    void setVerticalAxis(bool enabled) { verticalAxis_ = enabled; reset(); }
    int calibrationFrames() const { return calibration_; }
    Result process(const uint8_t *image, uint32_t now) {
        using namespace GestureConfig::Detection;
        Result result;
        const uint32_t elapsedFrame = now - lastFrame_;
        const bool gap = haveTime_ && elapsedFrame > MaxFrameGapMs;
        haveTime_ = true;
        lastFrame_ = now;
        if (calibration_ < CalibrationFrames) {
            ++calibration_;
            if (calibration_ == CalibrationFrames) state_ = State::Ready;
            std::memcpy(previous_, image, Pixels);
            result.state = state_;
            return result;
        }
        if (gap || elapsedFrame == 0) {
            std::memcpy(previous_, image, Pixels);
            block(now);
            result.state = state_;
            return result;
        }
        const auto flow = flow_.estimate(previous_, image, verticalAxis_);
        std::memcpy(previous_, image, Pixels);
        result.inliers = flow.inliers;
        result.movingPatches = flow.moving;
        const float along = verticalAxis_ ? flow.y * Width / Height : flow.x;
        const float across = verticalAxis_ ? flow.x * Height / Width : flow.y;
        // Project onto the configured gesture axis after aspect correction.
        // Mirroring is still applied only at the transport boundary.
        result.alongFlow = along * axisCos_ + across * axisSin_;
        result.acrossFlow = across * axisCos_ - along * axisSin_;
        result.globalMotion = flow.global;
        const bool axisMotion = !flow.global && flow.inliers >= MinInliers &&
                                std::fabs(result.alongFlow) >= MinFrameMotion &&
                                std::fabs(result.alongFlow) * 1000 >= elapsedFrame * MinSpeedPixelsPerSecond &&
                                std::fabs(result.alongFlow) >= std::fabs(result.acrossFlow) * AxisDominance;
        const bool quiet = flow.moving < QuietMovingPatches;
        const bool fastGesture = updateFastMotion(result, axisMotion, elapsedFrame, now);
        if (state_ == State::WaitClear) {
            if (!quiet) clearing_ = false;
            else if (!clearing_) { clearing_ = true; clearSince_ = now; }
            if (clearing_ && now - clearSince_ >= QuietMs && now - blockedAt_ >= CooldownMs) {
                state_ = State::Ready;
                clearing_ = false;
            }
        } else if (flow.global) {
            block(now);
        } else if (axisMotion) {
            if (state_ != State::Tracking) {
                state_ = State::Tracking;
                startedAt_ = now;
                travel_ = path_ = cross_ = 0;
                samples_ = 0;
            }
            lastMotion_ = now;
            travel_ += result.alongFlow;
            path_ += std::fabs(result.alongFlow);
            cross_ += std::fabs(result.acrossFlow);
            ++samples_;
            result.travel = travel_;
            if (now - startedAt_ > MaxGestureMs || std::fabs(travel_) < path_ * MinNetTravelRatio) {
                state_ = State::Ready;
            } else if (((samples_ >= MinMotionFrames && now - startedAt_ >= MinGestureMs &&
                         std::fabs(travel_) >= MinTravel) || fastGesture) &&
                       cross_ < std::fabs(travel_) * MaxCrossTravelRatio) {
                result.direction = travel_ > 0 ? Direction::Right : Direction::Left;
                block(now);
            }
        } else if (state_ == State::Tracking && now - lastMotion_ > MotionTimeoutMs) {
            state_ = State::Ready;
        }
        result.state = state_;
        return result;
    }
private:
    bool updateFastMotion(const Result &result, bool axisMotion, uint32_t frameMs, uint32_t now) {
        using namespace GestureConfig::Detection;
        if (state_ == State::WaitClear || !axisMotion || frameMs > FastMaxFrameGapMs ||
            result.inliers < FastMinInliers || std::fabs(result.alongFlow) < FastMinFrameMotion) {
            fastSamples_ = 0;
            fastTravel_ = 0;
            return false;
        }
        if (fastSamples_ == 0 || fastTravel_ * result.alongFlow <= 0) {
            fastSamples_ = 0;
            fastTravel_ = 0;
            fastStartedAt_ = now;
        }
        ++fastSamples_;
        fastTravel_ += result.alongFlow;
        return fastSamples_ >= 2 && now - fastStartedAt_ >= FastMinGestureMs &&
               std::fabs(fastTravel_) >= FastMinTravel;
    }

    void block(uint32_t now) {
        state_ = State::WaitClear;
        blockedAt_ = now;
        clearing_ = false;
        fastSamples_ = 0;
        fastTravel_ = 0;
    }
    OpticalFlow flow_;
    const float axisCos_ = std::cos(GestureConfig::Detection::AxisTiltDegrees * 0.01745329252f);
    const float axisSin_ = std::sin(GestureConfig::Detection::AxisTiltDegrees * 0.01745329252f);
    uint8_t previous_[Pixels] = {};
    int calibration_ = 0, samples_ = 0;
    State state_ = State::Calibrating;
    bool verticalAxis_ = false, haveTime_ = false, clearing_ = false;
    uint32_t lastFrame_ = 0, startedAt_ = 0, lastMotion_ = 0, blockedAt_ = 0, clearSince_ = 0;
    float travel_ = 0, path_ = 0, cross_ = 0;
    int fastSamples_ = 0;
    uint32_t fastStartedAt_ = 0;
    float fastTravel_ = 0;
};
