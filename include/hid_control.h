#pragma once
#include <Arduino.h>
#include <USBHID.h>
#include "hid_control_protocol.h"

// The USB task only copies bounded reports. Camera/HID state has one owner: loop().
class HidControl : public USBHIDDevice {
    using Descriptor = uint8_t[23];
    static const Descriptor &descriptor() {
        static const Descriptor data = {
        0x06, 0x00, 0xff, // Vendor usage page
        0x09, 0x53, 0xa1, 0x01, // Application collection
        0x85, HidControlProtocol::ReportId,
        0x09, 0x01, 0x15, 0x00, 0x26, 0xff, 0x00,
        0x75, 0x08, 0x95, 0x3f, 0xb1, 0x02, 0xc0 // 63-byte Feature
        };
        return data;
    }
    USBHID hid_;
    HidControlProtocol::Server server_;
    portMUX_TYPE mutex_ = portMUX_INITIALIZER_UNLOCKED;
    uint8_t request_[HidControlProtocol::Size] = {}, reply_[HidControlProtocol::Size] = {};
    bool pending_ = false, processing_ = false;
    uint32_t receivedAt_ = 0;
public:
    HidControl() {
        USBHID::addDevice(this, sizeof(Descriptor));
        std::memcpy(reply_, "AGH1", 4);
        reply_[4] = HidControlProtocol::Info;
    }
    uint16_t _onGetDescriptor(uint8_t *buffer) override {
        std::memcpy(buffer, descriptor(), sizeof(Descriptor)); return sizeof(Descriptor);
    }
    uint16_t _onGetFeature(uint8_t id, uint8_t *buffer, uint16_t length) override {
        if (id != HidControlProtocol::ReportId || length < sizeof(reply_)) return 0;
        portENTER_CRITICAL(&mutex_);
        std::memcpy(buffer, reply_, sizeof(reply_));
        portEXIT_CRITICAL(&mutex_);
        return sizeof(reply_);
    }
    void _onSetFeature(uint8_t id, const uint8_t *buffer, uint16_t length) override {
        if (id != HidControlProtocol::ReportId || length != sizeof(request_)) return;
        portENTER_CRITICAL(&mutex_);
        if (!pending_ && !processing_) {
            std::memcpy(request_, buffer, sizeof(request_));
            receivedAt_ = millis(); pending_ = true;
        }
        portEXIT_CRITICAL(&mutex_);
    }
    template<class Target>
    void poll(Target &target, bool measurement) {
        uint8_t request[HidControlProtocol::Size], reply[HidControlProtocol::Size];
        uint32_t receivedAt;
        portENTER_CRITICAL(&mutex_);
        if (!pending_) { portEXIT_CRITICAL(&mutex_); return; }
        std::memcpy(request, request_, sizeof(request));
        receivedAt = receivedAt_; pending_ = false; processing_ = true;
        portEXIT_CRITICAL(&mutex_);
#if GESTURE_MEASUREMENT_ONLY
        measurement = true;
#endif
#if GESTURE_SEQUENCE_EXPERIMENT
        constexpr uint8_t maxSteps = 3;
#else
        constexpr uint8_t maxSteps = 1;
#endif
        // Never execute a command that outlived the client's request timeout.
        const bool stale = millis() - receivedAt >= 1000;
        const uint8_t originalOp = request[4];
        if (stale) request[4] = 0;
        server_.handle(request, reply, target, measurement, maxSteps, millis());
        if (stale) { reply[4] = originalOp; reply[5] = HidControlProtocol::Expired; }
        portENTER_CRITICAL(&mutex_);
        std::memcpy(reply_, reply, sizeof(reply_)); processing_ = false;
        portEXIT_CRITICAL(&mutex_);
    }
};
