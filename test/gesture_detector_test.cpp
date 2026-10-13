#include "gesture_detector.h"
#include "gesture_output.h"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>

using Detector = GestureDetector;
using Direction = Detector::Direction;
using Image = std::array<uint8_t, Detector::Pixels>;

Image scene(int x = -100, int y = 10, int light = 0, bool dark = false) {
    Image image{};
    for (int row = 0; row < Detector::Height; ++row) {
        for (int col = 0; col < Detector::Width; ++col) {
            const bool hand = col >= x && col < x + 8 && row >= y && row < y + 10;
            image[row * Detector::Width + col] = hand ? (dark ? 10 : 190) + light
                                                       : 70 + (col * 3 + row * 5) % 20 + light;
        }
    }
    return image;
}

struct Fixture {
    Detector detector;
    uint32_t now = 0;
    unsigned events = 0;
    Direction direction = Direction::None;
    explicit Fixture(uint32_t start = 0) : now(start) {
        detector.reset();
        for (int i = 0; i < Detector::CalibrationFrames; ++i) step(scene());
        assert(events == 0);
    }
    Detector::Result step(const Image &image, uint32_t dt = 50) {
        now += dt;
        auto result = detector.process(image.data(), now);
        if (result.direction != Direction::None) {
            ++events;
            direction = result.direction;
        }
        return result;
    }
    void clear() { for (int i = 0; i < 25; ++i) step(scene()); }
    void swipe(bool right, bool dark = false) {
        for (int i = 0; i <= 12; ++i) step(scene(right ? i * 2 : 24 - i * 2, 10, 0, dark));
    }
};

int main() {
    {
        // Do not interpret a minority of sideways matches as a horizontal swipe
        // when most moving patches actually travel on the perpendicular axis.
        int x[14] = {-2, -2, -2, -2};
        int y[14];
        for (int &value : y) value = 1;
        const auto flow = OpticalFlow::summarize(x, y, 14, 40, false);
        assert(flow.moving == 14 && flow.inliers == 0 && flow.x == 0 && flow.y == 0);
        int boundaryX[12] = {-2, -2, -2, -2, -2, -2};
        int boundaryY[12] = {0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1};
        const auto boundary = OpticalFlow::summarize(boundaryX, boundaryY, 12, 40, false);
        assert(boundary.inliers == 6 && boundary.x == -2 && boundary.y == 0);
    }
    {
        // Two positive-speed clusters plus a few opposing vectors: the median
        // is unsupported, but six fast vectors form a usable group.
        int along[] = {1,1,1,1,1,2,2,2,4,5,5,6,6,6,-1,-1,-2};
        int across[17] = {};
        for (bool axisY : {false, true}) {
            for (int sign : {1, -1}) {
                int motion[17];
                for (int i = 0; i < 17; ++i) motion[i] = along[i] * sign;
                const auto flow = OpticalFlow::summarize(axisY ? across : motion,
                                                        axisY ? motion : across, 17, 40, axisY);
                assert(flow.inliers == 6);
                assert((axisY ? flow.y : flow.x) * sign > 5);
            }
        }
    }
    {
        int x[13] = {};
        int y[] = {1,1,1,1,1,1,1,1,5,5,5,5,5};
        const auto flow = OpticalFlow::summarize(x, y, 13, 40, true);
        assert(flow.inliers == 8 && flow.y == 1); // Keep a well-supported median.
    }
    {
        int x[13] = {};
        int y[] = {1,1,1,2,2,5,5,5,5,-1,-1,-1,-1};
        const auto flow = OpticalFlow::summarize(x, y, 13, 40, true);
        assert(flow.y == 0); // Four fast vectors are insufficient for fallback.
    }
    // A fast swipe moves six image pixels per 50 ms frame. Check both camera
    // axes and signs, including the two-strong-motion-frame confirmation gate.
    for (bool axisY : {false, true}) {
        for (bool right : {false, true}) {
            Fixture f;
            f.detector.setVerticalAxis(axisY);
            for (int i = 0; i < Detector::CalibrationFrames; ++i) f.step(scene());
            for (int i = 0; i < 4; ++i) {
                const int pos = right ? i * 6 : (3 - i) * 6;
                const auto result = f.step(axisY ? scene(12, pos) : scene(pos));
                if (i > 0) {
                    const float expected = (right ? 6.0f : -6.0f) *
                                           (axisY ? float(Detector::Width) / Detector::Height : 1.0f);
                    assert(std::fabs(result.alongFlow - expected) < 0.5f);
                }
                assert(f.events == (i >= 2 ? 1u : 0u));
            }
            assert(f.direction == (right ? Direction::Right : Direction::Left));
            // The immediate return is still suppressed at this speed.
            for (int i = 0; i < 4; ++i) {
                const int pos = right ? (3 - i) * 6 : i * 6;
                f.step(axisY ? scene(12, pos) : scene(pos));
            }
            assert(f.events == 1);
        }
    }
    // The mounted camera sees natural swipes with a cross-axis component.
    // Accept this moderate slant symmetrically, but not steep/vertical motion.
    for (bool axisY : {false, true}) {
        for (bool right : {false, true}) {
            Fixture f;
            f.detector.setVerticalAxis(axisY);
            for (int i = 0; i < Detector::CalibrationFrames; ++i) f.step(scene());
            for (int i = 0; i < 4; ++i) {
                const int step = right ? i : 3 - i;
                f.step(axisY ? scene(26 - 4 * step, 2 + 4 * step)
                             : scene(2 + 4 * step, 18 - 2 * step));
            }
            assert(f.events == 1 && f.direction == (right ? Direction::Right : Direction::Left));
            Fixture steep;
            steep.detector.setVerticalAxis(axisY);
            for (int i = 0; i < Detector::CalibrationFrames; ++i) steep.step(scene());
            for (int i = 0; i < 4; ++i) {
                const int step = right ? i : 3 - i;
                steep.step(axisY ? scene(26 - 6 * step, 2 + 2 * step)
                                 : scene(2 + 2 * step, 18 - 4 * step));
            }
            assert(steep.events == 0);
        }
    }
    {
        Fixture f;
        f.step(scene(0));
        f.step(scene(6));
        assert(f.events == 0); // One strong jump is insufficient.
        f.step(scene(0));
        assert(f.events == 0); // Two opposing jumps must not use fast confirmation.
    }
    {
        Fixture f(UINT32_MAX - 1325);
        f.step(scene(0), 33);
        f.step(scene(6), 33);
        assert(f.events == 0);
        f.step(scene(12), 33); // Fast confirmation at 30 Hz, across millis wrap.
        assert(f.events == 1 && f.direction == Direction::Right);
    }
    {
        Fixture f;
        f.step(scene(0));
        f.step(scene(6));
        f.step(scene(6)); // Stationary frame interrupts consecutive fast evidence.
        f.step(scene(12));
        assert(f.events == 0);
    }
    {
        Fixture f;
        f.step(scene(0));
        f.step(scene(6));
        f.step(scene(12), 101); // Beyond the fast path's frame-gap limit.
        assert(f.events == 0);
    }
    // Capture + flow processing can stretch real swipe intervals beyond 75 ms.
    // Strong consecutive motion must still work on either axis and direction.
    for (bool axisY : {false, true}) {
        for (bool right : {false, true}) {
            for (uint32_t dt : {87u, 100u}) {
                Fixture f;
                f.detector.setVerticalAxis(axisY);
                for (int i = 0; i < Detector::CalibrationFrames; ++i) f.step(scene());
                for (int i = 0; i < 3; ++i) {
                    const int pos = right ? i * 6 : (2 - i) * 6;
                    f.step(axisY ? scene(12, pos) : scene(pos), dt);
                    assert(f.events == (i == 2 ? 1u : 0u));
                }
                assert(f.direction == (right ? Direction::Right : Direction::Left));
            }
        }
    }
    {
        Fixture f;
        f.step(scene(0));
        f.step(scene(3));
        f.step(scene(6));
        assert(f.events == 0); // Smaller movements still need normal confirmation.
        f.step(scene(9));
        assert(f.events == 1 && f.direction == Direction::Right);
    }
    {
        Fixture f;
        for (int i = 0; i < 100; ++i) {
            Image blank{};
            blank.fill(static_cast<uint8_t>(i % 2 ? 100 : 30));
            f.step(blank);
        }
        assert(f.events == 0); // No texture means no trustworthy optical flow.
    }
    {
        Fixture f;
        f.swipe(true);
        assert(f.events == 1 && f.direction == Direction::Right);
        f.swipe(false); // Return motion must not trigger a second event.
        assert(f.events == 1);
        f.clear();
        f.swipe(false);
        assert(f.events == 2 && f.direction == Direction::Left);
    }
    {
        Fixture f;
        f.swipe(false, true); // A dark foreground also works.
        assert(f.events == 1 && f.direction == Direction::Left);
    }
    {
        Fixture f;
        // Hand gradually enters at the border, including 30 pixels before 40.
        for (int x = -8; x <= 24; ++x) f.step(scene(x));
        assert(f.events == 1 && f.direction == Direction::Right);
    }
    {
        Fixture f;
        for (int i = 0; i < 100; ++i) f.step(scene());
        for (int i = 0; i < 100; ++i) f.step(scene(12 + i % 2));
        assert(f.events == 0); // Static hand and jitter.
    }
    {
        Fixture f;
        for (int y = 0; y <= 20; ++y) f.step(scene(12, y));
        assert(f.events == 0); // Vertical motion.
    }
    {
        Fixture f;
        f.detector.setVerticalAxis(true);
        for (int i = 0; i < Detector::CalibrationFrames; ++i) f.step(scene());
        for (int y = 0; y <= 20; ++y) f.step(scene(12, y));
        assert(f.events == 1 && f.direction == Direction::Right);
        f.clear();
        for (int y = 20; y >= 0; --y) f.step(scene(12, y));
        assert(f.events == 2 && f.direction == Direction::Left);
        f.clear();
        f.swipe(true);
        assert(f.events == 2); // The orthogonal axis is rejected after rotation.
    }
    {
        Fixture f;
        for (int i = 0; i < 100; ++i) f.step(scene(-100, 10, (i % 3) * 30));
        assert(f.events == 0); // Global lighting flashes.
    }
    {
        Fixture f;
        for (int x = 0; x <= 4; x += 2) f.step(scene(x));
        f.step(scene());
        f.step(scene(24));
        f.step(scene(28));
        assert(f.events == 0); // Never join two disconnected appearances.
    }
    {
        Fixture f;
        for (int x = 0; x <= 4; x += 2) f.step(scene(x));
        f.step(scene(14), 1000);
        f.step(scene(18));
        assert(f.events == 0); // Capture outage cancels the trajectory.
        f.clear();
        f.swipe(true);
        assert(f.events == 1);
    }
    {
        Fixture f;
        for (int x = 0; x <= 24; ++x) f.step(scene(x), 200);
        assert(f.events == 0); // Slow background motion.
    }
    {
        Fixture f;
        for (int x = 0; x <= 24; x += 2) {
            auto image = scene(x, 1);
            for (int y = 18; y < 28; ++y)
                for (int col = 30 - x; col < 38 - x; ++col)
                    image[y * Detector::Width + col] = 190;
            f.step(image);
        }
        assert(f.events == 0); // Equal opposing motion has no dominant direction.
    }
    {
        Fixture f(UINT32_MAX - 1500);
        f.swipe(true); // Includes millis() wrap during the gesture.
        assert(f.events == 1);
        f.clear();
        f.swipe(false);
        assert(f.events == 2);
    }
    {
        Fixture f;
        f.swipe(true);
        f.detector.reset();
        for (int i = 0; i < Detector::CalibrationFrames; ++i) f.step(scene());
        f.swipe(false);
        assert(f.events == 2);
    }
    assert(gestureCommand(Direction::None, true) == nullptr);
    assert(std::strcmp(gestureCommand(Direction::Left, false), "ALT_LEFT") == 0);
    assert(std::strcmp(gestureCommand(Direction::Right, false), "ALT_RIGHT") == 0);
    assert(std::strcmp(gestureCommand(Direction::Left, true), "ALT_RIGHT") == 0);
    assert(std::strcmp(gestureCommand(Direction::Right, true), "ALT_LEFT") == 0);
    std::cout << "PASS: fast motion on both axes, direction, rearm, return suppression, dark foreground, jitter, vertical,\n"
                 "axis rotation, textureless scenes, lighting, lost frames, slow motion,\n"
                 "opposing motion, timer wrap, reset, mapping\n";
}
