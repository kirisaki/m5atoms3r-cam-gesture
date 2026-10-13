#pragma once
#include <esp_camera.h>
#include "gesture_config.h"

// Owns the camera driver boundary and frame timestamps. Frames are always
// returned to the driver before read() returns; callers receive grayscale only.
class CameraCapture {
public:
    static constexpr int Width = GestureConfig::Image::Width;
    static constexpr int Height = GestureConfig::Image::Height;
    static constexpr int Pixels = Width * Height;
    enum class CaptureResult { Ready, NoNewFrame, Failed, InvalidFrame };

    bool begin();
    CaptureResult read(uint8_t (&gray)[Pixels]);
    void resetHistory() { lastFrameUs_ = -1; }
    esp_err_t error() const { return error_; }
    unsigned sensorId() const;

private:
    int64_t lastFrameUs_ = -1;
    esp_err_t error_ = ESP_OK;
};
