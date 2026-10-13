#pragma once
#include <cstdint>

#ifndef GESTURE_MIN_SPEED_PPS
#define GESTURE_MIN_SPEED_PPS 10
#endif

// Defaults verified with the current mounting and recorded swipes.
// Pixel distances below refer to the downsampled image, not the camera frame.
namespace GestureConfig {
namespace Image {
constexpr int Width = 40;
constexpr int Height = 30;
}
namespace Flow {
constexpr int PatchRadius = 2;
constexpr int PatchStep = 2;
constexpr int SearchRadius = 6;
constexpr int MinTexture = 150;       // Sum of absolute deviations from patch mean.
constexpr int MinStationaryCost = 100;
constexpr int MaxMatchCost = 500;
// Keep integer ratio arithmetic to preserve matching at threshold boundaries.
constexpr int ImprovementNumerator = 3;
constexpr int ImprovementDenominator = 4;
constexpr int AlternativeDistance = 2;
constexpr int UniquenessMargin = 25;
constexpr int MinMovingPatches = 3;
constexpr int MinDirectionVotes = 3;
constexpr int DirectionVoteRatio = 2;
constexpr int MinDirectionCoveragePercent = 50; // Includes patches moving only across the axis.
constexpr int InlierDistance = 1;
constexpr int MinInliers = 3;
constexpr int InlierVoteRatio = 2;     // Inliers must cover at least half the axis votes.
// Fallback only when the median has insufficient support across mixed speeds.
constexpr int FastClusterMinAxisMotion = 3;
constexpr int FastClusterMinInliers = 5;
constexpr int FastClusterVoteRatio = 3;
constexpr int GlobalMinPatches = 60;
constexpr int GlobalPercent = 70;
}
namespace Detection {
constexpr int CalibrationFrames = 25;
constexpr uint32_t MaxFrameGapMs = 250;
constexpr int MinInliers = 3;
constexpr float MinFrameMotion = 0.6f;
constexpr int MinSpeedPixelsPerSecond = GESTURE_MIN_SPEED_PPS;
static_assert(MinSpeedPixelsPerSecond > 0, "Minimum flow speed must be positive");
// Compensate the slant observed in left swipes at the current mounting.
// Angle is measured from the selected, aspect-corrected image axis toward across.
constexpr float AxisTiltDegrees = -10.0f;
constexpr float AxisDominance = 1.5f;
constexpr int QuietMovingPatches = 3; // Fewer than this counts as quiet.
constexpr uint32_t QuietMs = 350;
constexpr uint32_t CooldownMs = 700;
constexpr uint32_t MaxGestureMs = 1500;
constexpr float MinNetTravelRatio = 0.7f;
constexpr int MinMotionFrames = 3;
constexpr uint32_t MinGestureMs = 80;
constexpr float MinTravel = 8;
constexpr float MaxCrossTravelRatio = 0.5f;
constexpr uint32_t MotionTimeoutMs = 150;
// A fast confirmation needs two consecutive, strong, same-direction flows.
constexpr int FastMinInliers = 4;
constexpr float FastMinFrameMotion = 4;
constexpr float FastMinTravel = 10;
constexpr uint32_t FastMaxFrameGapMs = 100; // Real swipes can produce 81-87 ms capture intervals.
constexpr uint32_t FastMinGestureMs = 25;
}
namespace Runtime {
constexpr bool VerticalAxis = true;
constexpr bool Mirror = true;
constexpr uint32_t CaptureIntervalMs = 33;
constexpr uint32_t ExposureWarmupMs = 1500;
constexpr uint32_t DiagnosticIntervalMs = 200;
constexpr uint32_t ImageTraceIntervalMs = CaptureIntervalMs;
}
}  // namespace GestureConfig
