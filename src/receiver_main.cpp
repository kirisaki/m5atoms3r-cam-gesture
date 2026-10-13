#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "gesture_protocol.h"
#include "receiver_display.h"

namespace {
using namespace GestureProtocol;
// M5Unified/M5GFX use controller 1 for internal devices and LCD backlight.
TwoWire bus(0);
ReceiverDisplay display;
struct Received { uint8_t bytes[PacketSize]; uint32_t receivedMs; };
QueueHandle_t packets = nullptr;
portMUX_TYPE countersLock = portMUX_INITIALIZER_UNLOCKED;
uint32_t dropped = 0;
EventTracker events;
uint32_t lastPacket = 0;
bool connected = false;

// Arduino-ESP32 2.x invokes this from its I2C task, not the interrupt handler.
void receive(int count) {
    Received packet{};
    int n = 0;
    while (bus.available()) {
        const int value = bus.read();
        if (n < static_cast<int>(PacketSize)) packet.bytes[n] = static_cast<uint8_t>(value);
        ++n;
    }
    packet.receivedMs = millis();
    if (count != static_cast<int>(PacketSize) || n != count ||
        xQueueSend(packets, &packet, 0) != pdTRUE) {
        portENTER_CRITICAL(&countersLock); ++dropped; portEXIT_CRITICAL(&countersLock);
    }
}
}
void setup() {
    Serial.begin(115200);
    display.begin();
    packets = xQueueCreate(16, sizeof(Received));
    if (!packets) {
        Serial.println("# [FAIL] RX queue allocation");
        display.fail("RX MEMORY FAILED");
        return;
    }
    bus.onReceive(receive);
    const bool ok = bus.begin(Address, Sda, Scl, Frequency);
    if (!ok) display.fail("I2C INIT FAILED");
    Serial.printf("# I2C receiver init=%s addr=0x%02x SDA=%d SCL=%d\n", ok ? "OK" : "FAIL", Address, Sda, Scl);
    lastPacket = millis();
}
void loop() {
    static uint32_t lastDropped = 0;
    static bool timeoutReported = false;
    Received packet;
    while (packets && xQueueReceive(packets, &packet, 0) == pdTRUE) {
        Frame frame;
        if (!decode(packet.bytes, sizeof(packet.bytes), frame)) {
            Serial.println("# [FAIL] Invalid I2C packet (version/fields/CRC)");
            display.fail("INVALID PACKET");
            continue;
        }
        // Account for time spent waiting in the receiver queue as well.
        const uint32_t queueAge = millis() - packet.receivedMs;
        if (queueAge > LinkTimeoutMs) continue;
        frame.uptimeMs += queueAge;
        lastPacket = packet.receivedMs;
        if (!connected) Serial.println("# I2C connected");
        connected = true; timeoutReported = false;
        const bool event = events.accept(frame);
        display.received(frame, event, millis());
        if (event)
            Serial.printf("GESTURE %s seq=%lu boot=%08lx\n", directionName(frame.direction), frame.eventSequence, frame.bootId);
        Serial.printf("# CAMERA state=%u error=%u flags=0x%02x failures=%lu uptime=%lu\n",
                      static_cast<unsigned>(frame.state), static_cast<unsigned>(frame.error), frame.flags, frame.failures, frame.uptimeMs);
    }
    if (!timeoutReported && millis() - lastPacket > LinkTimeoutMs) {
        Serial.println("# [FAIL] I2C timeout: no valid camera packet for 1500ms");
        connected = false; timeoutReported = true;
        display.disconnected();
    }
    portENTER_CRITICAL(&countersLock);
    const uint32_t currentDropped = dropped;
    portEXIT_CRITICAL(&countersLock);
    if (currentDropped != lastDropped) {
        Serial.printf("# [FAIL] RX dropped=%lu (length/queue overflow)\n", currentDropped);
        lastDropped = currentDropped;
        display.fail("RX DROPPED");
    }
    display.draw(millis());
    delay(1);
}
