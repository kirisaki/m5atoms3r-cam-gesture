#pragma once
#include <Arduino.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include "hid_shortcut.h"
#include "shortcut_protocol.h"

#if ARDUINO_USB_MODE
#error Camera HID requires ARDUINO_USB_MODE=0 (USB OTG/TinyUSB)
#endif

class GestureHid {
    struct Transport {
        USBHID hid;
        bool ready() { return hid.ready(); }
        bool send(const uint8_t (&report)[8]) {
            return hid.SendReport(HID_REPORT_ID_KEYBOARD, report, sizeof(report), 20);
        }
    } transport_;
    // Register the standard keyboard descriptor before Arduino starts USB.
    USBHIDKeyboard keyboard_;
    HidShortcut<Transport> shortcut_{transport_};
    uint32_t sent_ = 0, skipped_ = 0;
public:
    void begin() {
#if GESTURE_MEASUREMENT_ONLY || GESTURE_SEQUENCE_HID
        shortcut_.setEnabled(false, millis());
#endif
        keyboard_.begin(); USB.begin();
    }
    void poll() { shortcut_.poll(millis()); }
    bool busy() { return shortcut_.busy(); }
    void emit(bool right) {
        const bool sent = shortcut_.trigger(right, millis());
        if (sent) ++sent_; else ++skipped_;
        const auto key = shortcut_.binding(right);
        Serial.printf("# HID %s %s modifiers=%u usage=%u\n", right ? "RIGHT" : "LEFT",
                      sent ? "SENT" : "SKIPPED", key.modifiers, key.usage);
    }
    uint32_t generation() const { return shortcut_.generation(); }
    void emitPath(uint8_t path, uint32_t generation) {
        const bool sent = shortcut_.triggerPath(path, millis(), generation);
        if (sent) ++sent_; else ++skipped_;
        char text[4]; ShortcutMap::pathText(path, text);
        Serial.printf("# HID PATH %s %s\n", text, sent ? "SENT" : "SKIPPED");
    }
    void setEnabled(bool enabled) {
#if GESTURE_MEASUREMENT_ONLY
        enabled = false;
#endif
        // Legacy '1' must not bypass a managed session's lease or STOP.
        if (enabled && shortcut_.managed()) {
            Serial.println("# HID managed: use SET or STANDALONE");
            return;
        }
        if (!enabled && shortcut_.managed()) shortcut_.stop(millis());
        else shortcut_.setEnabled(enabled, millis());
        Serial.printf("# hid_enabled=%d\n", shortcut_.enabled());
    }
    const HidShortcut<Transport> &state() const { return shortcut_; }
    const char *apply(const ShortcutProtocol::Request &request, bool measurement) {
        using ShortcutProtocol::Action;
        const uint32_t now = millis();
        shortcut_.poll(now);
#if GESTURE_MEASUREMENT_ONLY
        measurement = true;
#endif
        const char *error = nullptr;
        switch (request.action) {
            case Action::Get: break;
            case Action::Set:
                if (measurement) error = "MEASUREMENT";
                else {
#if !GESTURE_SEQUENCE_EXPERIMENT
                    for (size_t i = 3; i < ShortcutMap::Slots; ++i)
                        if (request.bindings.keys[i].usage) error = "UNSUPPORTED_PATH";
#endif
                    if (!error) shortcut_.configure(request.bindings, request.enabled, now);
                }
                break;
            case Action::Keep:
                if (!shortcut_.keepAlive(now)) error = "EXPIRED";
                break;
            case Action::Stop: shortcut_.stop(now); break;
            case Action::Standalone:
                if (measurement) error = "MEASUREMENT";
                else shortcut_.standalone(now);
                break;
        }
        return error;
    }
    void command(const ShortcutProtocol::Request &request, bool measurement) {
        const char *error = apply(request, measurement);
        if (error) {
            Serial.printf("@G%u %lu ERR %s\n", request.version, static_cast<unsigned long>(request.id), error);
            return;
        }
        if (request.version == 2) {
            const auto &map = shortcut_.bindings();
#if GESTURE_SEQUENCE_EXPERIMENT
            constexpr int maxSteps = 3;
#else
            constexpr int maxSteps = 1;
#endif
            Serial.printf("@G2 %lu OK ATOMS3R_CAM %s %d %d %d %u",
                          static_cast<unsigned long>(request.id), shortcut_.managed() ? "MANAGED" : "STANDALONE",
                          shortcut_.enabled(), shortcut_.leaseActive(), maxSteps, static_cast<unsigned>(map.count()));
            for (size_t i = 1; i < ShortcutMap::Slots; ++i) {
                const auto key = map.keys[i];
                if (!key.usage) continue;
                char path[4]; ShortcutMap::pathText(i, path);
                Serial.printf(" %s %u %u", path, key.modifiers, key.usage);
            }
            Serial.println();
            return;
        }
        const auto left = shortcut_.binding(false), right = shortcut_.binding(true);
        Serial.printf("@G1 %lu OK ATOMS3R_CAM %s %d %d %u %u %u %u\n",
                      static_cast<unsigned long>(request.id), shortcut_.managed() ? "MANAGED" : "STANDALONE",
                      shortcut_.enabled(), shortcut_.leaseActive(),
                      left.modifiers, left.usage, right.modifiers, right.usage);
    }
    void printStatus() {
        Serial.printf("# HID enabled=%d ready=%d busy=%d sent=%lu skipped=%lu\n",
                      shortcut_.enabled(), transport_.ready(), shortcut_.busy(), sent_, skipped_);
    }
};
