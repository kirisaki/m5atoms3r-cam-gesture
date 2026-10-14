#pragma once
#include <cstddef>
#include <cstdint>

// Wire format is explicit: no compiler-dependent struct layout on the bus.
namespace GestureProtocol {
constexpr uint8_t Address = 0x42;
constexpr int Sda = 1, Scl = 2;
constexpr uint32_t Frequency = 100000;
constexpr uint32_t HeartbeatMs = 500, LinkTimeoutMs = 1500, EventMaxAgeMs = 1000;
constexpr size_t PacketSize = 28; // Legacy v1.
constexpr size_t DisplayPacketSize = 38;
constexpr uint32_t DisplayUpdateMs = 100;
enum class Phase : uint8_t { None, Tracking, Complete, Cancel, Cue, Waiting };
enum class CancelReason : uint8_t { None, Timeout, TooManySegments, Global, FrameGap };
enum class Direction : uint8_t { None, Left, Right, Up, Down };
enum class State : uint8_t { Starting, Calibrating, Ready, Tracking, WaitClear, Paused, Fault };
enum class Error : uint8_t { None, NoPsram, CameraInit, Capture, InvalidFrame, StaleFrame };
enum Flag : uint8_t { Enabled = 1, CameraReady = 2, ImageAxisY = 4, Mirrored = 8 };
struct Frame {
    uint8_t version = 1;
    Phase phase = Phase::None;
    CancelReason reason = CancelReason::None;
    uint8_t count = 0;
    Direction parts[3] = {};
    uint16_t remainingMs = 0, durationMs = 0;
    Direction direction = Direction::None; // Last event, retained in heartbeats.
    State state = State::Starting;
    Error error = Error::None;
    uint8_t flags = 0;
    uint32_t bootId = 0, eventSequence = 0, eventMs = 0, uptimeMs = 0, failures = 0;
};
inline uint16_t crc16(const uint8_t *data, size_t size) {
    uint16_t crc = 0xffff; // CRC-16/CCITT-FALSE, polynomial 0x1021.
    for (size_t i = 0; i < size; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    return crc;
}
inline void put32(uint8_t *p, uint32_t value) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(value >> (8 * i));
}
inline uint32_t get32(const uint8_t *p) {
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value |= static_cast<uint32_t>(p[i]) << (8 * i);
    return value;
}
inline void encode(const Frame &f, uint8_t (&p)[PacketSize]) {
    p[0] = 0xa7; p[1] = 1;
    p[2] = static_cast<uint8_t>(f.direction); p[3] = static_cast<uint8_t>(f.state);
    p[4] = static_cast<uint8_t>(f.error); p[5] = f.flags;
    put32(p + 6, f.bootId); put32(p + 10, f.eventSequence);
    put32(p + 14, f.eventMs); put32(p + 18, f.uptimeMs); put32(p + 22, f.failures);
    const uint16_t crc = crc16(p, 26);
    p[26] = static_cast<uint8_t>(crc); p[27] = static_cast<uint8_t>(crc >> 8);
}
inline void encodeDisplay(const Frame &f, uint8_t (&p)[DisplayPacketSize]) {
    uint8_t legacy[PacketSize];
    encode(f, legacy);
    for (size_t i = 0; i < 26; ++i) p[i] = legacy[i];
    p[1] = 2;
    p[26] = static_cast<uint8_t>(f.phase); p[27] = static_cast<uint8_t>(f.reason);
    p[28] = f.count;
    for (int i = 0; i < 3; ++i) p[29+i] = static_cast<uint8_t>(f.parts[i]);
    p[32] = f.remainingMs & 255; p[33] = f.remainingMs >> 8;
    p[34] = f.durationMs & 255; p[35] = f.durationMs >> 8;
    const uint16_t crc = crc16(p, 36);
    p[36] = crc & 255; p[37] = crc >> 8;
}
inline bool decode(const uint8_t *p, size_t size, Frame &f) {
    if (!p || (size != PacketSize && size != DisplayPacketSize)) return false;
    const bool extended = size == DisplayPacketSize;
    if (p[0] != 0xa7 || p[1] != (extended ? 2 : 1) ||
        p[2] > static_cast<uint8_t>(Direction::Down) ||
        p[3] > static_cast<uint8_t>(State::Fault) ||
        p[4] > static_cast<uint8_t>(Error::StaleFrame) || (p[5] & 0xf0) ||
        crc16(p, size - 2) != (static_cast<uint16_t>(p[size-2]) | static_cast<uint16_t>(p[size-1]) << 8))
        return false;
    Frame decoded;
    if (extended) {
        if (p[26] > static_cast<uint8_t>(Phase::Waiting) ||
            p[27] > static_cast<uint8_t>(CancelReason::FrameGap) || p[28] > 3) return false;
        decoded.phase = static_cast<Phase>(p[26]); decoded.reason = static_cast<CancelReason>(p[27]);
        decoded.count = p[28];
        for (int i = 0; i < 3; ++i) {
            if (i < decoded.count ? p[29+i] == 0 || p[29+i] > 4 : p[29+i] != 0) return false;
            decoded.parts[i] = static_cast<Direction>(p[29+i]);
        }
        decoded.remainingMs = p[32] | static_cast<uint16_t>(p[33]) << 8;
        decoded.durationMs = p[34] | static_cast<uint16_t>(p[35]) << 8;
        if (decoded.remainingMs > decoded.durationMs ||
            (decoded.phase != Phase::Cancel && decoded.reason != CancelReason::None) ||
            ((decoded.phase == Phase::Tracking || decoded.phase == Phase::Complete) && !decoded.count) ||
            ((decoded.phase == Phase::None || decoded.phase == Phase::Cue || decoded.phase == Phase::Waiting) && decoded.count)) return false;
    }
    decoded.version = p[1];
    decoded.direction = static_cast<Direction>(p[2]); decoded.state = static_cast<State>(p[3]);
    decoded.error = static_cast<Error>(p[4]); decoded.flags = p[5];
    decoded.bootId = get32(p + 6); decoded.eventSequence = get32(p + 10);
    decoded.eventMs = get32(p + 14); decoded.uptimeMs = get32(p + 18); decoded.failures = get32(p + 22);
    f = decoded;
    return true;
}
inline uint16_t remainingAfter(uint16_t remaining, uint32_t elapsed) {
    return elapsed >= remaining ? 0 : static_cast<uint16_t>(remaining - elapsed);
}
class EventTracker {
public:
    bool accept(const Frame &f) {
        const bool changed = !seen_ || boot_ != f.bootId || sequence_ != f.eventSequence;
        seen_ = true; boot_ = f.bootId; sequence_ = f.eventSequence;
        return changed && f.direction != Direction::None && f.uptimeMs - f.eventMs <= EventMaxAgeMs;
    }
private:
    bool seen_ = false;
    uint32_t boot_ = 0, sequence_ = 0;
};
inline const char *directionName(Direction direction) {
    switch (direction) {
        case Direction::Left: return "LEFT";
        case Direction::Right: return "RIGHT";
        case Direction::Up: return "UP";
        case Direction::Down: return "DOWN";
        default: return "NONE";
    }
}
} // namespace GestureProtocol
