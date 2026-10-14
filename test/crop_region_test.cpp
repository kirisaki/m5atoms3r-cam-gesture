#include "gesture_detector.h"
#include <array>
#include <cassert>
#include <iostream>

using Image = std::array<uint8_t, GestureDetector::Pixels>;

static Image scene(int left, int top) {
    Image image{};
    for (int y = 0; y < OpticalFlow::Height; ++y)
        for (int x = 0; x < OpticalFlow::Width; ++x)
            image[y * OpticalFlow::Width + x] =
                x >= left && x < left + 8 && y >= top && y < top + 10
                    ? 190 : 70 + (x * 3 + y * 5) % 20;
    return image;
}

static void changeExcluded(Image &image, unsigned seed) {
    for (int y = 0; y < OpticalFlow::Height; ++y)
        for (int x = 0; x < GestureConfig::Flow::MinX; ++x) {
            seed = seed * 1664525u + 1013904223u;
            image[y * OpticalFlow::Width + x] = seed >> 24;
        }
}

int main() {
    static_assert(GestureConfig::Flow::MinX == 10, "Test bottom quarter in CW270 mounting");
    OpticalFlow flow;
    // Changes confined to the excluded bottom must not influence texture,
    // matching, direction votes or global-motion detection. Exercise both
    // the current image and the previous image's search area at the boundary.
    for (bool axisY : {false, true}) {
        auto a = scene(10, 4), b = scene(10, 10);
        const auto expected = flow.estimate(a.data(), b.data(), axisY);
        for (unsigned seed = 1; seed <= 8; ++seed) {
            auto previous = a, current = b;
            changeExcluded(previous, seed);
            changeExcluded(current, seed + 97);
            const auto actual = flow.estimate(previous.data(), current.data(), axisY);
            assert(actual.x == expected.x && actual.y == expected.y);
            assert(actual.inliers == expected.inliers && actual.moving == expected.moving);
            assert(actual.textured == expected.textured && actual.global == expected.global);
        }
    }
    for (int left : {0, 12}) {
        for (int sign : {-1, 1}) {
            GestureDetector detector;
            detector.setVerticalAxis(true);
            uint32_t now = 0;
            for (int i = 0; i < GestureDetector::CalibrationFrames; ++i) {
                now += 50;
                detector.process(scene(-100, 0).data(), now);
            }
            int events = 0;
            for (int i = 0; i < 4; ++i) {
                const int position = sign > 0 ? i * 6 : (3 - i) * 6;
                now += 50;
                const auto result = detector.process(scene(left, position).data(), now);
                if (result.direction != GestureDetector::Direction::None) {
                    ++events;
                    assert(result.direction == (sign > 0 ? GestureDetector::Direction::Right : GestureDetector::Direction::Left));
                }
            }
            assert(events == (left == 0 ? 0 : 1));
        }
    }
    std::cout << "PASS: excluded-region isolation and retained swipes in both directions\n";
}
