#include "receiver_view.h"
#include "sequence_presentation.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace GestureProtocol;
int main() {
    Frame f; f.version = 2; f.state = State::Ready;
    GestureSequence sequence;
    GestureSequence::Motion motion; motion.x = 4; motion.inliers = motion.moving = 8;
    uint32_t now = 50;
    presentSequence(f, sequence.update(motion, now, 50), now);
    now += 50; presentSequence(f, sequence.update(motion, now, 50), now);
    assert(f.phase == Phase::Tracking && f.count == 1 && f.parts[0] == Direction::Right);
    const uint32_t segment = f.eventSequence;
    motion = {};
    for (int i = 0; i < 8; ++i) {
        now += 50; presentSequence(f, sequence.update(motion, now, 50), now);
    }
    assert(f.phase == Phase::Complete && f.remainingMs == 1500 && f.eventSequence == segment + 1);
    now += 50; presentSequence(f, sequence.update(motion, now, 50), now);
    assert(f.count == 1 && f.phase == Phase::Complete && f.remainingMs == 1450);
    for (int i = 0; i < 29; ++i) {
        now += 50; presentSequence(f, sequence.update(motion, now, 50), now);
    }
    assert(f.phase == Phase::None && !f.count && !f.remainingMs);
    GestureSequence::Result cancelled;
    cancelled.event = GestureSequence::Event::Cancel; cancelled.reason = CancelReason::Timeout;
    cancelled.count = 2; cancelled.parts[0] = Direction::Right; cancelled.parts[1] = Direction::Down;
    cancelled.waiting = true; cancelled.remainingMs = 1500;
    presentSequence(f, cancelled, now);
    cancelled = {}; cancelled.waiting = true; cancelled.remainingMs = 1400;
    presentSequence(f, cancelled, now + 100);
    assert(f.phase == Phase::Cancel && f.count == 2 && f.reason == CancelReason::Timeout);

    ReceiverView view;
    assert(std::strcmp(view.title(0), "WAITING") == 0);
    f.phase = Phase::Tracking; f.state = State::Tracking;
    f.remainingMs = 400; f.durationMs = 400;
    view.received(f, false, 0xfffffff0);
    assert(view.remaining(0x20) == 352); // Local clock wrap.
    assert(view.remaining(500) == 0);
    assert(std::strcmp(view.title(500), "TRACKING") == 0); // Never predict DONE.
    f.phase = Phase::Complete; f.state = State::WaitClear; f.remainingMs = 1500; f.durationMs = 1500;
    view.received(f, true, 600);
    assert(std::strcmp(view.title(2300), "DONE") == 0); // Never predict READY.
    f.phase = Phase::None; f.state = State::Ready; f.count = 0;
    view.received(f, false, 2350);
    assert(std::strcmp(view.title(2350), "READY") == 0);
    f.phase = Phase::Cue; f.durationMs = f.remainingMs = 1000; f.eventMs = f.uptimeMs = 3000;
    view.received(f, true, 3000);
    assert(std::strcmp(view.title(3000), "START") == 0);
    assert(std::strcmp(view.title(4000), "READY") == 0);
    f.uptimeMs = 5000; view.received(f, false, 5000);
    assert(view.phase(5000) == Phase::None); // Stale cue after reconnect.
    f.error = Error::Capture; view.received(f, false, 5000);
    assert(std::strcmp(view.title(5000), "ERROR") == 0);
    view.disconnected();
    assert(!view.frame.count && std::strcmp(view.title(5000), "NO SIGNAL") == 0);

    f = {}; f.state = State::Ready; f.direction = Direction::Right;
    view.received(f, true, 100);
    assert(view.frame.count == 1 && view.phase(100) == Phase::Tracking);
    view.received(f, false, 600);
    assert(view.phase(600) == Phase::Tracking);
    assert(view.phase(1300) == Phase::None); // Heartbeat doesn't renew legacy flash.
    ++f.bootId; view.received(f, false, 700);
    assert(!view.frame.count); // No path carried into a new camera boot.
    std::cout << "PASS: result retention, timing, old protocol, cue, fault and disconnect\n";
}
