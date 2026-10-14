#pragma once
#include "camera_capture.h"
#include "gesture_detector.h"
#include "gesture_i2c.h"
#include "gesture_hid.h"
#if GESTURE_SEQUENCE_EXPERIMENT
#include "sequence_detector.h"
#if !GESTURE_MEASUREMENT_ONLY
#error Sequence experiment must disable HID at build time
#endif
using CameraDetector = SequenceDetector;
#else
using CameraDetector = GestureDetector;
#endif

// Owns camera runtime state. Recognition stays independent of Arduino;
// console commands and formatting live in camera_console.cpp.
class CameraApp {
public:
    void begin();
    void tick();
private:
    CameraCapture camera;
    CameraDetector detector;
    GestureI2c gestureBus;
    GestureHid gestureHid;
    ShortcutProtocol::Framer shortcutFrames;
    GestureProtocol::Error captureError = GestureProtocol::Error::None;
    uint32_t lastGoodFrame = 0;
    uint8_t gray[GestureDetector::Pixels] = {};
    bool cameraReady = false;
    bool enabled = true;
    bool diagnostics = false;
    bool mirror = GestureConfig::Runtime::Mirror;
    bool verticalAxis = GestureConfig::Runtime::VerticalAxis;  // Verified against recorded right/left swipes in this mounting.
    bool imageTrace = false;
    bool measurement = false;
    uint32_t previousFlowMs = 0;
#if GESTURE_SEQUENCE_EXPERIMENT
    uint32_t previousSequenceMs = 0;
#endif
    uint32_t lastImage = 0;
    uint32_t frames = 0, events = 0, failures = 0;
    uint32_t lastCapture = 0, lastDiagnostic = 0, warmupStarted = 0;
    uint32_t lastProcessingMs = 0;
    esp_err_t cameraError = ESP_OK;
    CameraDetector::Result latest;

    void calibrate();
    void capture();
    void emitGesture(GestureDetector::Direction direction);
    void publishStatus();
    void handleCommands();
    void status();
    void traceImage(uint32_t now);
    void reportDetection(uint32_t now);
};
