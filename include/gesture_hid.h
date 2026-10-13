#pragma once
#include <Arduino.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include "hid_shortcut.h"

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
#if GESTURE_MEASUREMENT_ONLY
        shortcut_.setEnabled(false, millis());
#endif
        keyboard_.begin(); USB.begin();
    }
    void poll() { shortcut_.poll(millis()); }
    bool busy() { return shortcut_.busy(); }
    void emit(bool right) {
        const bool sent = shortcut_.trigger(right, millis());
        if (sent) ++sent_; else ++skipped_;
        Serial.printf("# HID %s %s\n", right ? "ALT_RIGHT" : "ALT_LEFT", sent ? "SENT" : "SKIPPED");
    }
    void setEnabled(bool enabled) {
#if GESTURE_MEASUREMENT_ONLY
        enabled = false;
#endif
        shortcut_.setEnabled(enabled, millis());
        Serial.printf("# hid_enabled=%d\n", shortcut_.enabled());
    }
    void printStatus() {
        Serial.printf("# HID enabled=%d ready=%d busy=%d sent=%lu skipped=%lu\n",
                      shortcut_.enabled(), transport_.ready(), shortcut_.busy(), sent_, skipped_);
    }
};
