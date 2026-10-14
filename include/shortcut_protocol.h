#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "hid_shortcut.h"

namespace ShortcutProtocol {
static constexpr size_t FrameCapacity = 1024;
enum class Action { Get, Set, Keep, Stop, Standalone };
struct Request {
    uint32_t id = 0;
    unsigned version = 1;
    ShortcutMap bindings;
    Action action = Action::Get;
    ShortcutBinding left, right;
    bool enabled = false;
};

// Once '@' starts a frame, ALL bytes belong to it until LF, even on overflow
// or a truncated connection. A reconnecting client sends LF before its first
// frame. Never release a malformed frame's tail to the legacy byte commands.
class Framer {
public:
    enum class Result { Legacy, Pending, Complete };
    Result feed(char byte) {
        if (!active_) {
            if (byte != '@') return Result::Legacy;
            active_ = true; overflow_ = false; size_ = 0;
        }
        if (byte == '\n') {
            active_ = false;
            if (size_ && buffer_[size_ - 1] == '\r') --size_;
            buffer_[size_] = '\0';
            return Result::Complete;
        }
        if (size_ < sizeof(buffer_) - 1) buffer_[size_++] = byte;
        else overflow_ = true;
        // Reject embedded NUL/non-ASCII control bytes instead of truncating parse.
        if ((static_cast<unsigned char>(byte) < 32 && byte != '\r') ||
            static_cast<unsigned char>(byte) > 126) overflow_ = true;
        return Result::Pending;
    }
    char *line() { return buffer_; }
    bool invalid() const { return overflow_; }
private:
    char buffer_[FrameCapacity] = {};
    size_t size_ = 0;
    bool active_ = false, overflow_ = false;
};

inline bool number(const char *text, uint32_t &value) {
    if (!*text) return false;
    value = 0;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9') return false;
        const uint32_t digit = *text - '0';
        if (value > (UINT32_MAX - digit) / 10) return false;
        value = value * 10 + digit;
    }
    return true;
}
inline bool validBinding(uint32_t modifiers, uint32_t usage) {
    return modifiers <= 255 && ((usage == 0 && modifiers == 0) || (usage >= 4 && usage <= 0x73));
}

// Mutates the completed frame only. Invalid SET never changes device state.
inline const char *parse(char *line, Request &request) {
    request = Request{};
    char *tokens[5 + 3 * ShortcutMap::MaxBindings];
    size_t count = 0;
    for (char *p = line; *p;) {
        while (*p == ' ') ++p;
        if (!*p) break;
        if (count == sizeof(tokens) / sizeof(tokens[0])) return "FORMAT";
        tokens[count++] = p;
        while (*p && *p != ' ') ++p;
        if (*p) *p++ = '\0';
    }
    if (count && !std::strcmp(tokens[0], "@G2")) request.version = 2;
    if (count < 3 || (std::strcmp(tokens[0], "@G1") && std::strcmp(tokens[0], "@G2")) ||
        !number(tokens[1], request.id) || !request.id) return "FORMAT";
    const char *op = tokens[2];
    if (!std::strcmp(op, "SET")) {
        request.action = Action::Set;
        if (request.version == 2) {
            uint32_t enabled, size;
            if (count < 5 || !number(tokens[3], enabled) || !number(tokens[4], size)) return "FORMAT";
            if (enabled > 1 || size > ShortcutMap::MaxBindings) return "RANGE";
            if (count != 5 + size * 3) return "FORMAT";
            bool seen[ShortcutMap::Slots] = {};
            for (size_t i = 0; i < size; ++i) {
                const auto path = ShortcutMap::parsePath(tokens[5 + i * 3]);
                uint32_t modifiers, usage;
                if (!path || seen[path]) return "PATH";
                seen[path] = true;
                if (!number(tokens[6 + i * 3], modifiers) || !number(tokens[7 + i * 3], usage)) return "FORMAT";
                if (!validBinding(modifiers, usage)) return "RANGE";
                request.bindings.keys[path] = ShortcutBinding(modifiers, usage);
            }
            request.enabled = enabled != 0;
            return nullptr;
        }
        if (count != 8) return "FORMAT";
        uint32_t args[5];
        for (size_t i = 0; i < 5; ++i) if (!number(tokens[i + 3], args[i])) return "FORMAT";
        if (!validBinding(args[0], args[1]) || !validBinding(args[2], args[3]) || args[4] > 1) return "RANGE";
        request.action = Action::Set;
        request.left = ShortcutBinding(args[0], args[1]);
        request.right = ShortcutBinding(args[2], args[3]);
        request.enabled = args[4] != 0;
        request.bindings.keys[1] = request.left; request.bindings.keys[2] = request.right;
    } else {
        if (count != 3) return "FORMAT";
        if (!std::strcmp(op, "GET")) request.action = Action::Get;
        else if (!std::strcmp(op, "KEEP")) request.action = Action::Keep;
        else if (!std::strcmp(op, "STOP")) request.action = Action::Stop;
        else if (!std::strcmp(op, "STANDALONE")) request.action = Action::Standalone;
        else return "COMMAND";
    }
    return nullptr;
}
} // namespace ShortcutProtocol
