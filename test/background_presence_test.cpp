#include "background_presence.h"
#include <array>
#include <cassert>
#include <iostream>

using Image = std::array<uint8_t, BackgroundPresence::Pixels>;
using Presence = BackgroundPresence;
static Image background() {
    Image image;
    for (int y = 0; y < Presence::Height; ++y)
        for (int x = 0; x < Presence::Width; ++x)
            image[y * Presence::Width + x] = 40 + (x * 11 + y * 17) % 90;
    return image;
}
static Image object(Image image, int left, int top) {
    for (int y = top; y < top + 10 && y < Presence::Height; ++y)
        for (int x = left; x < left + 10 && x < Presence::Width; ++x)
            image[y * Presence::Width + x] = 220;
    return image;
}
int main() {
    const auto bg = background();
    Presence p;
    assert(p.evaluate(bg.data()).state == Presence::State::Unknown);
    for (int i = 0; i < Presence::Samples; ++i) p.calibrate(bg.data());
    assert(p.evaluate(bg.data()).state == Presence::State::Absent);
    for (int left : {12, 30}) {
        const auto hand = object(bg, left, 8);
        // Never learn a stationary hand into the background, even at an edge.
        for (int frame = 0; frame < 100; ++frame)
            assert(p.evaluate(hand.data()).state == Presence::State::Present);
        assert(p.evaluate(bg.data()).state == Presence::State::Absent);
    }
    auto exposure = bg;
    for (auto &value : exposure) value = value * 1.2f + 15;
    const auto compensated = p.evaluate(exposure.data());
    assert(compensated.state == Presence::State::Absent && compensated.largest == 0);
    assert(p.evaluate(object(exposure, 15, 8).data()).state == Presence::State::Present);
    auto cropped = bg;
    for (int y = 0; y < Presence::Height; ++y)
        for (int x = 0; x < Presence::MinX; ++x) cropped[y * Presence::Width + x] = 255;
    assert(p.evaluate(cropped.data()).changed == 0);
    auto noise = bg;
    for (int i = 0; i < 10; ++i) noise[(i * 2) * Presence::Width + 15] = 255;
    assert(p.evaluate(noise.data()).state == Presence::State::Absent);
    auto ambiguous = bg;
    for (int y = 5; y < 11; ++y) for (int x = 15; x < 21; ++x) ambiguous[y * Presence::Width + x] = 220;
    assert(p.evaluate(ambiguous.data()).state == Presence::State::Unknown);
    auto covered = bg;
    covered.fill(220);
    assert(p.evaluate(covered.data()).state != Presence::State::Absent);
    p.reset();
    assert(p.evaluate(bg.data()).state == Presence::State::Unknown);
    std::cout << "PASS: stationary foreground, edge hold, exposure compensation, ROI, noise, ambiguous and covered frames\n";
}
