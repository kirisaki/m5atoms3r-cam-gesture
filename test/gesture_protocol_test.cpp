#include <cassert>
#include <cstdio>
#include <cstring>
#include "gesture_protocol.h"
using namespace GestureProtocol;

int main() {
    const uint8_t reference[] = "123456789";
    assert(crc16(reference, 9) == 0x29b1);
    Frame f;
    f.direction = Direction::Right; f.state = State::WaitClear;
    f.error = Error::Capture; f.flags = Enabled | CameraReady | Mirrored;
    f.bootId = 0x12345678; f.eventSequence = 0xfffffffe;
    f.eventMs = 0xfffffff0; f.uptimeMs = 0x20; f.failures = 0x89abcdef;
    uint8_t p[PacketSize];
    encode(f, p);
    const uint8_t expected[] = {
        0xa7, 1, 2, 4, 3, 0x0b, 0x78, 0x56, 0x34, 0x12,
        0xfe, 0xff, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xff,
        0x20, 0, 0, 0, 0xef, 0xcd, 0xab, 0x89
    };
    assert(std::memcmp(p, expected, sizeof(expected)) == 0);
    Frame decoded;
    assert(decode(p, sizeof(p), decoded));
    assert(decoded.direction == f.direction && decoded.state == f.state && decoded.error == f.error);
    assert(decoded.flags == f.flags && decoded.bootId == f.bootId && decoded.eventSequence == f.eventSequence);
    assert(decoded.eventMs == f.eventMs && decoded.uptimeMs == f.uptimeMs && decoded.failures == f.failures);
    for (size_t i = 0; i < PacketSize; ++i) {
        for (int bit = 0; bit < 8; ++bit) {
            p[i] ^= 1 << bit;
            assert(!decode(p, sizeof(p), decoded));
            p[i] ^= 1 << bit;
        }
    }
    assert(!decode(p, PacketSize - 1, decoded));
    assert(!decode(p, PacketSize + 1, decoded));
    assert(!decode(nullptr, 0, decoded));
    // Correct CRC must not make unknown enum values or flags acceptable.
    const uint8_t invalid[] = {0, 2, 5, 7, 6, 0x10};
    for (size_t i = 0; i < sizeof(invalid); ++i) {
        encode(f, p); p[i] = invalid[i];
        const uint16_t crc = crc16(p, 26);
        p[26] = crc & 255; p[27] = crc >> 8;
        assert(!decode(p, PacketSize, decoded));
    }
    EventTracker tracker;
    f.error = Error::None;
    assert(tracker.accept(f)); // Uptime wrap still yields a recent event.
    assert(!tracker.accept(f));
    ++f.eventSequence; assert(tracker.accept(f));
    ++f.eventSequence; assert(tracker.accept(f)); // Event sequence wrap to zero.
    assert(!tracker.accept(f));
    ++f.bootId; assert(tracker.accept(f)); // Camera reboot, same sequence.
    ++f.eventSequence; f.uptimeMs = f.eventMs + EventMaxAgeMs + 1;
    assert(!tracker.accept(f)); // Reconnection must not replay an old action.
    f.uptimeMs = f.eventMs; assert(!tracker.accept(f)); // Already consumed/expired.
    ++f.eventSequence; f.uptimeMs = f.eventMs + EventMaxAgeMs;
    assert(tracker.accept(f));
    ++f.eventSequence; f.direction = Direction::None; assert(!tracker.accept(f));
    puts("gesture protocol tests passed");
}
