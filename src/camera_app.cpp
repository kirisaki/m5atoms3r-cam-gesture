#include "camera_app.h"
#include "gesture_output.h"

using namespace GestureConfig::Runtime;

void CameraApp::calibrate() {
    detector.setVerticalAxis(verticalAxis);
    latest = {};
    previousFlowMs = 0;
    warmupStarted = millis();
    camera.resetHistory();
    lastGoodFrame = millis();
    Serial.println("# CALIBRATING: optical flow warmup; keep camera still for 3 seconds.");
}

void CameraApp::emitGesture(GestureDetector::Direction direction) {
    if (const char *command = gestureCommand(direction, mirror)) {
        ++events;
        Serial.println(command);
        const bool right = (direction == GestureDetector::Direction::Right) != mirror;
        gestureBus.emit(right ? GestureProtocol::Direction::Right : GestureProtocol::Direction::Left, millis());
        gestureHid.emit(right);
    }
}

void CameraApp::publishStatus() {
    using namespace GestureProtocol;
    State state = State::Starting;
    switch (latest.state) {
        case GestureDetector::State::Calibrating: state = State::Calibrating; break;
        case GestureDetector::State::Ready: state = State::Ready; break;
        case GestureDetector::State::Tracking: state = State::Tracking; break;
        case GestureDetector::State::WaitClear: state = State::WaitClear; break;
    }
    Error error = captureError;
    if (cameraReady && enabled && millis() - lastGoodFrame > 1000) error = Error::StaleFrame;
    if (!enabled) state = State::Paused;
    if (error != Error::None) state = State::Fault;
    const uint8_t flags = (enabled ? Enabled : 0) | (cameraReady ? CameraReady : 0) |
                          (verticalAxis ? ImageAxisY : 0) | (mirror ? Mirrored : 0);
    gestureBus.update(state, error, flags, failures, millis());
}

void CameraApp::capture() {
    switch (camera.read(gray)) {
        case CameraCapture::CaptureResult::NoNewFrame: return;
        case CameraCapture::CaptureResult::Failed:
            ++failures;
            captureError = GestureProtocol::Error::Capture;
            Serial.println("# [FAIL] Camera capture failed.");
            return;
        case CameraCapture::CaptureResult::InvalidFrame:
            ++failures;
            captureError = GestureProtocol::Error::InvalidFrame;
            Serial.println("# [FAIL] Invalid camera frame.");
            return;
        case CameraCapture::CaptureResult::Ready: ++frames; break;
    }
    const uint32_t now = millis();
    lastGoodFrame = now;
    captureError = GestureProtocol::Error::None;
    traceImage(now);
    // Let automatic exposure settle before initializing frame-to-frame tracking.
    if (now - warmupStarted < ExposureWarmupMs) return;
    const bool calibrating = detector.calibrationFrames() < GestureDetector::CalibrationFrames;
    const uint32_t flowStart = millis();
    latest = detector.process(gray, now);
    lastProcessingMs = millis() - flowStart;
    if (measurement) {
        Serial.printf("# FLOW %lu %lu %.3f %.3f %d %d %d %.3f %d\n",
                      now, previousFlowMs ? now - previousFlowMs : 0,
                      latest.alongFlow, latest.acrossFlow, latest.inliers,
                      latest.movingPatches, latest.globalMotion, latest.travel,
                      static_cast<int>(latest.state));
    }
    previousFlowMs = now;
    if (calibrating && latest.state == GestureDetector::State::Ready) {
        Serial.println("# READY: swipe horizontally, then pause briefly before the next swipe.");
    }
    emitGesture(latest.direction);
    reportDetection(now);
}

void CameraApp::begin() {
    // Windows SerialPort.Close() can emit the CDC bootloader control sequence.
    // Only the explicit 'u' command should stop this always-on HID/I2C device.
    Serial.enableReboot(false);
    gestureHid.begin();
    Serial.begin(115200);
    const uint32_t started = millis();
    while (!Serial && millis() - started < 3000) delay(10);
    Serial.println("\n# Starting AtomS3R-CAM optical flow + I2C + USB HID...");
    gestureBus.begin();
    publishStatus();
    if (psramFound()) {
        cameraReady = camera.begin();
        cameraError = camera.error();
        if (cameraReady) {
            Serial.printf("# [OK] Camera initialized: sensor PID=0x%02x (GC0308=0x9b)\n",
                          camera.sensorId());
        } else {
            captureError = GestureProtocol::Error::CameraInit;
            Serial.printf("# [FAIL] esp_camera_init: 0x%x (%s)\n",
                          static_cast<unsigned>(cameraError), esp_err_to_name(cameraError));
        }
    } else {
        captureError = GestureProtocol::Error::NoPsram;
        cameraError = ESP_ERR_NO_MEM;
        Serial.println("# [FAIL] PSRAM not found.");
    }
    calibrate();
    publishStatus();
    status();
}

void CameraApp::tick() {
    gestureHid.poll();
    handleCommands();
    // Finish key release before another potentially blocking camera capture.
    if (!gestureHid.busy() && cameraReady && enabled && millis() - lastCapture >= CaptureIntervalMs) {
        lastCapture = millis();
        capture();
    }
    publishStatus();
    delay(1);
}
