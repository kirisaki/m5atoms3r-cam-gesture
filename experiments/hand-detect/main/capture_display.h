#pragma once
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "gesture_protocol.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Uses the existing receiver firmware. A fresh green RIGHT arrow is a capture
// cue, not a detected gesture. The receiver's Count therefore counts cues here.
class CaptureDisplay {
public:
    void begin() {
        using namespace GestureProtocol;
        frame_.bootId = esp_random();
        frame_.state = State::Ready;
        frame_.flags = Flag::CameraReady;
        i2c_master_bus_config_t bus{};
        bus.i2c_port = I2C_NUM_0; // Camera SCCB owns controller 1.
        bus.sda_io_num = static_cast<gpio_num_t>(Sda);
        bus.scl_io_num = static_cast<gpio_num_t>(Scl);
        bus.clk_source = I2C_CLK_SRC_DEFAULT;
        bus.glitch_ignore_cnt = 7;
        bus.flags.enable_internal_pullup = true;
        esp_err_t result = i2c_new_master_bus(&bus, &bus_);
        if (result == ESP_OK) {
            i2c_device_config_t device{};
            device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
            device.device_address = Address;
            device.scl_speed_hz = Frequency;
            result = i2c_master_bus_add_device(bus_, &device, &device_);
        }
        ESP_LOGI("capture_display", "init=%s address=0x%02x", esp_err_to_name(result), Address);
        if (device_) send();
    }

    void tick() {
        if (esp_timer_get_time() - lastSend_ >= GestureProtocol::HeartbeatMs * 1000LL) send();
    }

    bool start() {
        frame_.state = GestureProtocol::State::Tracking;
        frame_.direction = GestureProtocol::Direction::Right;
        ++frame_.eventSequence;
        // A transient NACK must not lose the one capture cue. Reuse the event
        // sequence so the receiver deduplicates any already accepted packet.
        for (int attempt = 0; attempt < 3; ++attempt) {
            frame_.eventMs = esp_timer_get_time() / 1000;
            if (send()) return true;
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        // Do not replay a failed cue in a later heartbeat.
        frame_.direction = GestureProtocol::Direction::None;
        frame_.state = GestureProtocol::State::Ready;
        return false;
    }

    void transferring() { frame_.state = GestureProtocol::State::Paused; send(); }
    void idle() { frame_.state = GestureProtocol::State::Ready; send(); }

private:
    GestureProtocol::Frame frame_;
    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t device_ = nullptr;
    int64_t lastSend_ = 0;
    esp_err_t lastResult_ = ESP_FAIL;
    bool attempted_ = false;

    bool send() {
        lastSend_ = esp_timer_get_time();
        if (!device_) return false;
        frame_.uptimeMs = lastSend_ / 1000;
        uint8_t packet[GestureProtocol::PacketSize];
        GestureProtocol::encode(frame_, packet);
        const esp_err_t result = i2c_master_transmit(device_, packet, sizeof(packet), 10);
        if (!attempted_ || result != lastResult_)
            ESP_LOGI("capture_display", "link=%s result=%s", result == ESP_OK ? "ACK" : "FAIL", esp_err_to_name(result));
        attempted_ = true;
        lastResult_ = result;
        return result == ESP_OK;
    }
};
