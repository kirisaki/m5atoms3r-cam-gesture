#pragma once
#include <cstddef>
#include <cstdint>

// Wire format is explicit: no compiler-dependent struct layout on the bus.
namespace GestureProtocol {
constexpr uint8_t Address = 0x42;
constexpr int Sda = 1, Scl = 2;
constexpr uint32_t Frequency = 100000;
constexpr uint32_t HeartbeatMs = 500, LinkTimeoutMs = 1500, EventMaxAgeMs = 1000;
constexpr size_t PacketSize = 28;
enum class Direction : uint8_t { None, Left, Right, Up, Down };
enum class State : uint8_t { Starting, Calibrating, Ready, Tracking, WaitClear, Paused, Fault };
enum class Error : uint8_t { None, NoPsram, CameraInit, Capture, InvalidFrame, StaleFrame };
enum Flag : uint8_t { Enabled = 1, CameraReady = 2, ImageAxisY = 4, Mirrored = 8 };
struct Frame {
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
inline bool decode(const uint8_t *p, size_t size, Frame &f) {
    if (size != PacketSize || p[0] != 0xa7 || p[1] != 1 ||
        p[2] > static_cast<uint8_t>(Direction::Down) ||
        p[3] > static_cast<uint8_t>(State::Fault) ||
        p[4] > static_cast<uint8_t>(Error::StaleFrame) || (p[5] & 0xf0) ||
        crc16(p, 26) != (static_cast<uint16_t>(p[26]) | static_cast<uint16_t>(p[27]) << 8))
        return false;
    f.direction = static_cast<Direction>(p[2]); f.state = static_cast<State>(p[3]);
    f.error = static_cast<Error>(p[4]); f.flags = p[5];
    f.bootId = get32(p + 6); f.eventSequence = get32(p + 10);
    f.eventMs = get32(p + 14); f.uptimeMs = get32(p + 18); f.failures = get32(p + 22);
    return true;
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
