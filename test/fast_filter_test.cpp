#include "gesture_detector.h"
#include <array>
#include <cassert>
#include <iostream>

using Detector = GestureDetector;
using Image = std::array<uint8_t, Detector::Pixels>;

static Image scene(int position, bool vertical) {
    Image image{};
    const int left = vertical ? 12 : position;
    const int top = vertical ? position : 10;
    for (int y = 0; y < Detector::Height; ++y)
        for (int x = 0; x < Detector::Width; ++x)
            image[y * Detector::Width + x] =
                x >= left && x < left + 8 && y >= top && y < top + 10
                    ? 190 : 70 + (x * 3 + y * 5) % 20;
    return image;
}

static void check(bool vertical, int sign, int step, uint32_t dt, bool accepted) {
    Detector detector;
    detector.setVerticalAxis(vertical);
    uint32_t now = 0;
    for (int i = 0; i < Detector::CalibrationFrames; ++i) {
        now += dt;
        detector.process(scene(-100, vertical).data(), now);
    }
    int count = 0;
    for (int i = 0; i <= 18 / step; ++i) {
        const int position = sign > 0 ? i * step : 18 - i * step;
        now += dt;
        const auto result = detector.process(scene(position, vertical).data(), now);
        if (result.direction != Detector::Direction::None) {
            ++count;
            assert(result.direction == (sign > 0 ? Detector::Direction::Right : Detector::Direction::Left));
        }
    }
    if (count != (accepted ? 1 : 0))
        std::cerr << "axisY=" << vertical << " sign=" << sign << " step=" << step
                  << " dt=" << dt << " events=" << count << " expected=" << accepted << '\n';
    assert(count == (accepted ? 1 : 0));
    // A fast return without a quiet interval must not add another event.
    if (accepted) {
        for (int i = 0; i <= 3; ++i) {
            now += 50;
            const int position = sign > 0 ? 18 - 6 * i : 6 * i;
            assert(detector.process(scene(position, vertical).data(), now).direction == Detector::Direction::None);
        }
    }
}

int main() {
    static_assert(GestureConfig::Detection::MinSpeedPixelsPerSecond == 40, "Use candidate setting");
    for (bool vertical : {false, true}) {
        for (int sign : {-1, 1}) {
            check(vertical, sign, 1, 50, false); // Slow motion with ample total travel.
            check(vertical, sign, 4, 50, true);
            check(vertical, sign, 6, 100, true); // Strong motion at stretched frame intervals.
        }
    }
    for (int sign : {-1, 1}) {
        // Identical displacements must be distinguished by elapsed time,
        // rather than a fixed per-frame displacement threshold.
        check(false, sign, 2, 80, false);
        check(false, sign, 2, 30, true);
    }
    std::cout << "PASS: fast filter speed, axes, directions, frame timing and return suppression\n";
}
