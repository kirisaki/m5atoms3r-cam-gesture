#include "camera_app.h"
#include <esp32-hal-tinyusb.h>

using namespace GestureConfig::Runtime;

namespace {
const char *stateName(GestureDetector::State state) {
    switch (state) {
        case GestureDetector::State::Calibrating: return "CALIBRATING";
        case GestureDetector::State::Ready: return "READY";
        case GestureDetector::State::Tracking: return "TRACKING";
        case GestureDetector::State::WaitClear: return "WAIT_CLEAR";
    }
    return "UNKNOWN";
}
}  // namespace

void CameraApp::status() {
    Serial.printf("# AtomS3R-CAM optical flow + I2C + USB HID uptime=%lu s\n", millis() / 1000);
    Serial.printf("# camera=%s error=0x%x psram=%u heap_free=%u frames=%lu failures=%lu events=%lu\n",
                  cameraReady ? "OK" : "FAIL", static_cast<unsigned>(cameraError),
                  ESP.getPsramSize(), ESP.getFreeHeap(), frames, failures, events);
    Serial.printf("# state=%s enabled=%d mirror=%d axis=%s debug=%d calibration=%d/%d\n",
                  stateName(latest.state), enabled, mirror, verticalAxis ? "Y" : "X", diagnostics,
                  detector.calibrationFrames(), GestureDetector::CalibrationFrames);
    Serial.printf("# gesture_axis_tilt=%.1f deg dominance=%.1f cross_limit=%.2f fast_gap_ms=%lu\n",
                  GestureConfig::Detection::AxisTiltDegrees, GestureConfig::Detection::AxisDominance,
                  GestureConfig::Detection::MaxCrossTravelRatio, GestureConfig::Detection::FastMaxFrameGapMs);
    Serial.printf("# min_speed_pps=%d\n", GestureConfig::Detection::MinSpeedPixelsPerSecond);
    Serial.printf("# flow_roi raw_x_min=%d bottom_crop_percent=%d upright_rotation=270\n",
                  GestureConfig::Flow::MinX, GestureConfig::Flow::BottomCropPercent);
    Serial.println("# Commands: i/h=info b=reset flow a=axis m=reverse d=debug p=pause/resume v=image trace");
    Serial.println("# HID: 0=off 1=on (standalone only) [=test left ]=test right");
    Serial.println("# Control: @G1 <id> GET/SET/KEEP/STOP/STANDALONE (LF terminated)");
    Serial.println("# USB: u=enter firmware download mode (stops camera until restart/upload)");
    Serial.println("# Measurement: r=start (HID off) s=stop c=right display cue (no key)");
    Serial.println("# Events: ALT_LEFT / ALT_RIGHT (legacy direction names; HID mapping may differ)");
    gestureBus.printStatus();
    gestureHid.printStatus();
}
void CameraApp::traceImage(uint32_t now) {
    if (imageTrace && now - lastImage >= ImageTraceIntervalMs) {
        lastImage = now;
        static char hex[GestureDetector::Pixels * 2 + 1];
        constexpr char digits[] = "0123456789abcdef";
        for (int i = 0; i < GestureDetector::Pixels; ++i) {
            hex[2 * i] = digits[gray[i] >> 4];
            hex[2 * i + 1] = digits[gray[i] & 15];
        }
        hex[GestureDetector::Pixels * 2] = '\0';
        Serial.printf("# IMAGE %lu %s\n", now, hex);
    }
}

void CameraApp::reportDetection(uint32_t now) {
    if (diagnostics && now - lastDiagnostic >= DiagnosticIntervalMs) {
        lastDiagnostic = now;
        Serial.printf("# state=%s inliers=%d moving=%d flow=%.1f,%.1f travel=%.1f global=%d flow_ms=%lu frames=%lu\n",
                      stateName(latest.state), latest.inliers, latest.movingPatches,
                      latest.alongFlow, latest.acrossFlow,
                      latest.travel, latest.globalMotion, lastProcessingMs, frames);
    }
}

void CameraApp::handleCommands() {
    while (Serial.available()) {
        const char byte = static_cast<char>(Serial.read());
        const auto result = shortcutFrames.feed(byte);
        if (result == ShortcutProtocol::Framer::Result::Pending) continue;
        if (result == ShortcutProtocol::Framer::Result::Complete) {
            ShortcutProtocol::Request request;
            const char *error = shortcutFrames.invalid() ? "FRAME" : ShortcutProtocol::parse(shortcutFrames.line(), request);
            if (error) Serial.printf("@G1 %lu ERR %s\n", static_cast<unsigned long>(request.id), error);
            else gestureHid.command(request, measurement);
            continue;
        }
        switch (byte) {
            case 'r':
                gestureHid.setEnabled(false);
                enabled = true;
                diagnostics = false;
                measurement = imageTrace = true;
                calibrate();
                Serial.println("# MEASUREMENT START");
                Serial.printf("# min_speed_pps=%d\n", GestureConfig::Detection::MinSpeedPixelsPerSecond);
                Serial.printf("# flow_roi raw_x_min=%d bottom_crop_percent=%d upright_rotation=270\n",
                              GestureConfig::Flow::MinX, GestureConfig::Flow::BottomCropPercent);
                break;
            case 's':
                measurement = imageTrace = false;
                Serial.println("# MEASUREMENT STOP");
                break;
            case 'c':
                if (measurement && latest.state == GestureDetector::State::Ready) {
                    const uint32_t now = millis();
                    gestureBus.emit(GestureProtocol::Direction::Right, now);
                    publishStatus();
                    Serial.printf("# TRIAL_CUE RIGHT ms=%lu ack=%d\n", now, gestureBus.acknowledged());
                } else Serial.println("# TRIAL_CUE NOT_READY");
                break;
            case '0': gestureHid.setEnabled(false); break;
            case '1': gestureHid.setEnabled(true); break;
            case '[': gestureHid.emit(false); break;
            case ']': gestureHid.emit(true); break;
            case 'u':
                gestureHid.setEnabled(false);
                Serial.println("# Entering firmware download mode");
                Serial.flush();
                usb_persist_restart(RESTART_BOOTLOADER);
                return;
            case 'i':
            case 'h': status(); break;
            case 'b': calibrate(); break;
            case 'v':
                imageTrace = !imageTrace;
                Serial.printf("# image_trace=%d\n", imageTrace);
                break;
            case 'a':
                verticalAxis = !verticalAxis;
                Serial.printf("# axis=%s\n", verticalAxis ? "Y" : "X");
                calibrate();
                break;
            case 'm':
                mirror = !mirror;
                Serial.printf("# mirror=%d\n", mirror);
                calibrate();
                break;
            case 'd':
                diagnostics = !diagnostics;
                Serial.printf("# debug=%d\n", diagnostics);
                break;
            case 'p':
                enabled = !enabled;
                Serial.printf("# enabled=%d\n", enabled);
                if (enabled) calibrate();
                break;
            default: break;
        }
    }
}
