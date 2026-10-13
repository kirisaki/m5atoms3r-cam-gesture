#pragma once
#include "gesture_detector.h"

inline const char *gestureCommand(GestureDetector::Direction direction, bool mirror) {
    if (direction == GestureDetector::Direction::None) return nullptr;
    const bool right = (direction == GestureDetector::Direction::Right) != mirror;
    return right ? "ALT_RIGHT" : "ALT_LEFT";
}
