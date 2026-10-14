#include "sequence_detector.h"
#include <array>
#include <cassert>
#include <iostream>
#include <limits>

using Sequence = GestureSequence;
using Direction = Sequence::Direction;
struct Trial {
    Sequence detector;
    uint32_t now = 0;
    Sequence::Result tick(float x = 0, float y = 0, uint32_t dt = 50) {
        Sequence::Motion m;
        m.x = x; m.y = y;
        m.inliers = m.moving = (x || y) ? 8 : 0;
        now += dt;
        return detector.update(m, now, dt);
    }
    void right() { tick(4); assert(tick(4).event == Sequence::Event::Segment); }
    Sequence::Result disappear() {
        Sequence::Result result;
        for (int i = 0; i < static_cast<int>(Sequence::StopQuietMs / 50); ++i) result = tick();
        return result;
    }
};
using Image = std::array<uint8_t, GestureDetector::Pixels>;
static Image scene(int left, int top) {
    Image image;
    for (int y = 0; y < OpticalFlow::Height; ++y) {
        for (int x = 0; x < OpticalFlow::Width; ++x) {
            int u = x - left, v = y - top;
            image[y * OpticalFlow::Width + x] = u >= 0 && u < 10 && v >= 0 && v < 10
                ? 100 + (u * 37 + v * 61 + u * v * 13) % 150 : 30 + (x * 7 + y * 11) % 30;
        }
    }
    return image;
}
int main() {
    {
        Trial t;
        t.right();
        assert(t.disappear().event == Sequence::Event::Complete);
        // Returning motion inside the delay cannot start or seed a new gesture.
        for (int i = 0; i < 20; ++i) {
            const auto r = t.tick(0, 4);
            assert(r.waiting && r.event == Sequence::Event::None);
        }
        // Quiet alone does not release the 1.5 second lockout early.
        for (int i = 0; i < 9; ++i) assert(t.tick().waiting);
        assert(!t.tick().waiting);
        t.right();
    }

    {
        Trial t;
        t.right();
        // A 150ms hesitation joins the same gesture; a longer hold completes.
        for (int i = 0; i < 3; ++i) assert(t.tick().event == Sequence::Event::None);
        t.tick(0, 4);
        auto r = t.tick(0, 4);
        assert(r.event == Sequence::Event::Segment && r.count == 2);
        assert(r.parts[0] == Direction::Right && r.parts[1] == Direction::Down);
        r = t.disappear();
        assert(r.event == Sequence::Event::Complete && r.idleMs == Sequence::StopQuietMs);
        assert(t.tick(4).event == Sequence::Event::None);
    }
    for (int axis = 0; axis < 2; ++axis) for (int sign : {-1, 1}) {
        Trial t;
        const float x = axis ? 0 : sign * 4, y = axis ? sign * 4 : 0;
        t.tick(x, y);
        auto r = t.tick(x, y);
        assert(r.event == Sequence::Event::Segment);
        const auto expected = axis ? (sign > 0 ? Direction::Down : Direction::Up)
                                   : (sign > 0 ? Direction::Right : Direction::Left);
        assert(r.parts[0] == expected);
        assert(t.tick(x, y).event == Sequence::Event::None);
        assert(t.disappear().event == Sequence::Event::Complete);
        // Holding for four seconds must not repeat, nor block the next attempt.
        for (int i = 0; i < 80; ++i) assert(t.tick().event == Sequence::Event::None);
        t.right();
    }
    {
        Trial t;
        t.right();
        // Diagonal motion extends tracking through a corner, without a new leg.
        for (int i = 0; i < 8; ++i) {
            const auto r = t.tick(3, 3);
            assert(r.event == Sequence::Event::None && r.tracking);
        }
        t.tick(0, -4);
        assert(t.tick(0, -4).parts[1] == Direction::Up);
        assert(t.disappear().event == Sequence::Event::Complete);
    }
    {
        Trial t;
        t.right();
        // Weak/noisy patches do not prevent completion or rearming.
        Sequence::Motion noise;
        noise.x = 4; noise.inliers = 3; noise.moving = 12;
        int completed = 0;
        for (int i = 0; i < 40; ++i) {
            t.now += 50;
            const auto r = t.detector.update(noise, t.now, 50);
            if (r.event == Sequence::Event::Complete) ++completed;
            assert(r.event != Sequence::Event::Cancel);
        }
        assert(completed == 1);
        t.right();
    }
    {
        Trial t;
        t.right(); t.tick(0, 4); t.tick(0, 4); t.tick(-4);
        auto r = t.tick(-4);
        assert(r.event == Sequence::Event::Segment && r.count == 3);
        assert(r.parts[0] == Direction::Right && r.parts[1] == Direction::Down && r.parts[2] == Direction::Left);
        assert(t.tick(-4).event == Sequence::Event::None); // Same third direction merges.
        r = t.disappear();
        assert(r.event == Sequence::Event::Complete && r.count == 3 && r.parts[2] == Direction::Left);
    }
    {
        Trial t;
        t.right(); t.tick(0, 4); t.tick(0, 4); t.tick(4);
        auto r = t.tick(4);
        assert(r.event == Sequence::Event::Segment && r.count == 3);
        assert(r.parts[2] == Direction::Right); // Non-adjacent repeated direction is a new leg.
        t.tick(0, -4);
        r = t.tick(0, -4);
        assert(r.event == Sequence::Event::Cancel && r.reason == Sequence::Reason::TooManySegments);
        assert(r.count == 3 && r.parts[2] == Direction::Right);
        assert(t.disappear().event != Sequence::Event::Complete);
    }
    {
        Trial t;
        t.right();
        const auto r = t.tick(0, 0, 300);
        assert(r.event == Sequence::Event::Cancel && r.reason == Sequence::Reason::FrameGap);
        assert(t.disappear().event != Sequence::Event::Complete);
    }
    {
        Trial t;
        t.right();
        Sequence::Motion camera;
        camera.global = true; camera.moving = 100;
        t.now += 50;
        const auto r = t.detector.update(camera, t.now, 50);
        assert(r.event == Sequence::Event::Cancel && r.reason == Sequence::Reason::Global);
    }
    {
        Trial t;
        for (int i = 0; i < 50; ++i) assert(t.tick(1).event == Sequence::Event::None);
        t.detector.reset(); t.right();
        Sequence::Result r;
        for (int i = 0; i < static_cast<int>(Sequence::MaxDurationMs / 50) + 1; ++i) r = t.tick(4);
        assert(r.event == Sequence::Event::Cancel && r.reason == Sequence::Reason::Timeout);
    }
    {
        Trial t;
        t.now = std::numeric_limits<uint32_t>::max() - 120;
        t.right();
        assert(t.disappear().event == Sequence::Event::Complete);
    }
    {
        int vx[] = {-4,-4,-4,-4,0,0}, vy[] = {0,0,0,0,4,4};
        constexpr int left = GestureConfig::Flow::MinX;
        uint8_t px[] = {left+2,left+4,left+4,left+6,32,34}, py[] = {10,12,14,16,8,10};
        auto f = OpticalFlow::summarize2D(vx, vy, px, py, 6, 60);
        assert(f.x == -4 && f.y == 0 && f.inliers == 4 && f.edgeVotes == 4);
        assert(f.centerX == left + 4 && f.centerY == 13);
    }
    {
        Trial t;
        t.tick(3);
        assert(t.tick(1.5f).event == Sequence::Event::None);
        assert(t.tick(2).event == Sequence::Event::Segment);
        t.tick(0, 2.5f);
        assert(t.tick(0, 2.5f).count == 2);
        assert(t.disappear().event == Sequence::Event::Complete);
    }
    {
        // Equal competing axes must not become an invented diagonal/sequence.
        int vx[] = {4,4,4,4,0,0,0,0}, vy[] = {0,0,0,0,4,4,4,4};
        uint8_t p[] = {16,18,20,22,24,26,28,30};
        const auto f = OpticalFlow::summarize2D(vx, vy, p, p, 8, 60);
        assert(f.inliers == 0 || (f.x == 4 && f.y == 0) || (f.y == 4 && f.x == 0));
    }
    {
        OpticalFlow flow;
        for (int axis = 0; axis < 2; ++axis) for (int sign : {-1, 1}) {
            const auto a = scene(20, 10), b = scene(20 + (axis ? 0 : sign * 3), 10 + (axis ? sign * 3 : 0));
            const auto f = flow.estimate2D(a.data(), b.data());
            assert(f.inliers >= 3);
            assert(std::fabs(f.x - (axis ? 0 : sign * 3)) < 1);
            assert(std::fabs(f.y - (axis ? sign * 3 : 0)) < 1);
            auto a2 = a, b2 = b;
            for (int y = 0; y < OpticalFlow::Height; ++y)
                for (int x = 0; x < GestureConfig::Flow::MinX; ++x) {
                    a2[y * OpticalFlow::Width + x] = (x * 53 + y * 31) % 256;
                    b2[y * OpticalFlow::Width + x] = (x * 17 + y * 79) % 256;
                }
            const auto f2 = flow.estimate2D(a2.data(), b2.data());
            assert(f.x == f2.x && f.y == f2.y && f.inliers == f2.inliers && f.edgeVotes == f2.edgeVotes);
        }
    }
    {
        SequenceDetector detector;
        detector.setVerticalAxis(true);
        uint32_t now = 0;
        for (int i = 0; i < SequenceDetector::CalibrationFrames; ++i)
            detector.process(scene(-100, -100).data(), now += 50);
        detector.process(scene(14, 10).data(), now += 50);
        detector.process(scene(19, 10).data(), now += 50);
        const auto r = detector.process(scene(24, 10).data(), now += 50);
        assert(r.sequence.event == Sequence::Event::Segment && r.sequence.parts[0] == Direction::Down);
    }
    std::cout << "PASS: direction flow, ROI isolation, short pause, hold completion/rearm, noise, cancellation and wraparound\n";
}
