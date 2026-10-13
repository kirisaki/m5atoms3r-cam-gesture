#include "camera_capture.h"
#include <Arduino.h>

namespace {
constexpr int CameraWidth = 320;
constexpr int CameraHeight = 240;
constexpr int ScaleX = CameraWidth / CameraCapture::Width;
constexpr int ScaleY = CameraHeight / CameraCapture::Height;
static_assert(CameraWidth % CameraCapture::Width == 0 &&
              CameraHeight % CameraCapture::Height == 0,
              "Analysis dimensions must divide the QVGA camera frame");
}  // namespace

bool CameraCapture::begin() {
    // AtomS3R-CAM GC0308 pin map:
    // https://docs.m5stack.com/en/core/AtomS3R-CAM
    pinMode(18, OUTPUT);
    digitalWrite(18, LOW);  // Camera power enable, active low.
    delay(500);
    camera_config_t config = {};
    config.pin_pwdn = -1;
    config.pin_reset = -1;
    config.pin_xclk = 21;
    config.pin_sccb_sda = 12;
    config.pin_sccb_scl = 9;
    config.pin_d0 = 3;
    config.pin_d1 = 42;
    config.pin_d2 = 46;
    config.pin_d3 = 48;
    config.pin_d4 = 4;
    config.pin_d5 = 17;
    config.pin_d6 = 11;
    config.pin_d7 = 13;
    config.pin_vsync = 10;
    config.pin_href = 14;
    config.pin_pclk = 40;
    config.xclk_freq_hz = 20000000;
    config.ledc_timer = LEDC_TIMER_0;
    config.ledc_channel = LEDC_CHANNEL_0;
    // GC0308 has no native JPEG output.
    config.pixel_format = PIXFORMAT_RGB565;
    config.frame_size = FRAMESIZE_QVGA;
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;
    error_ = esp_camera_init(&config);
    return error_ == ESP_OK;
}

unsigned CameraCapture::sensorId() const {
    return esp_camera_sensor_get()->id.PID;
}

CameraCapture::CaptureResult CameraCapture::read(uint8_t (&gray)[Pixels]) {
    camera_fb_t *frame = esp_camera_fb_get();
    if (!frame) {
        return CaptureResult::Failed;
    }
    if (frame->format != PIXFORMAT_RGB565 || frame->width != CameraWidth ||
        frame->height != CameraHeight || frame->len != CameraWidth * CameraHeight * 2) {
        esp_camera_fb_return(frame);
        return CaptureResult::InvalidFrame;
    }
    const int64_t timestamp = static_cast<int64_t>(frame->timestamp.tv_sec) * 1000000 +
                              frame->timestamp.tv_usec;
    if (timestamp <= lastFrameUs_) {
        esp_camera_fb_return(frame);
        return CaptureResult::NoNewFrame;
    }
    lastFrameUs_ = timestamp;
    // Average each source cell to suppress pixel noise and retain the whole image.
    for (int y = 0; y < Height; ++y) {
        for (int x = 0; x < Width; ++x) {
            uint32_t total = 0;
            for (int dy = 0; dy < ScaleY; ++dy) {
                for (int dx = 0; dx < ScaleX; ++dx) {
                    const size_t pos = ((y * ScaleY + dy) * CameraWidth + x * ScaleX + dx) * 2;
                    const uint16_t rgb = (frame->buf[pos] << 8) | frame->buf[pos + 1];
                    const unsigned r = ((rgb >> 11) & 31) * 255 / 31;
                    const unsigned g = ((rgb >> 5) & 63) * 255 / 63;
                    const unsigned b = (rgb & 31) * 255 / 31;
                    total += (77 * r + 150 * g + 29 * b) >> 8;
                }
            }
            gray[y * Width + x] = total / (ScaleX * ScaleY);
        }
    }
    esp_camera_fb_return(frame);
    return CaptureResult::Ready;
}
