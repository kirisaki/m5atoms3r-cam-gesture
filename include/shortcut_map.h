#pragma once
#include <cstddef>
#include <cstdint>

struct ShortcutBinding {
    uint8_t modifiers, usage;
    ShortcutBinding(uint8_t modifiers = 0, uint8_t usage = 0) : modifiers(modifiers), usage(usage) {}
};

// Base-5 path digits match Direction: L=1, R=2, U=3, D=4.
// Zero digits and repeated adjacent directions are invalid. 52 valid paths.
struct ShortcutMap {
    static constexpr size_t Slots = 125, MaxBindings = 52;
    ShortcutBinding keys[Slots] = {};
    static uint8_t parsePath(const char *text) {
        unsigned path = 0, previous = 0, count = 0;
        for (; *text; ++text) {
            const unsigned digit = *text == 'L' ? 1 : *text == 'R' ? 2 : *text == 'U' ? 3 : *text == 'D' ? 4 : 0;
            if (!digit || digit == previous || ++count > 3) return 0;
            path = path * 5 + digit;
            previous = digit;
        }
        return static_cast<uint8_t>(path);
    }
    static bool validPath(unsigned path) {
        if (!path || path >= Slots) return false;
        unsigned previous = 0;
        while (path) {
            const unsigned digit = path % 5;
            if (!digit || digit == previous) return false;
            previous = digit; path /= 5;
        }
        return true;
    }
    static void pathText(unsigned path, char (&text)[4]) {
        char reversed[3]; unsigned count = 0;
        if (!validPath(path)) { text[0] = 0; return; }
        while (path) { reversed[count++] = " LRUD"[path % 5]; path /= 5; }
        for (unsigned i = 0; i < count; ++i) text[i] = reversed[count - i - 1];
        text[count] = 0;
    }
    size_t count() const {
        size_t n = 0;
        for (const auto &key : keys) if (key.usage) ++n;
        return n;
    }
    static ShortcutMap defaults() {
        ShortcutMap map;
        map.keys[1] = {4, 0x50}; map.keys[2] = {4, 0x4f};
        return map;
    }
};
