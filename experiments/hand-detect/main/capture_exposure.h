#pragma once
#include <cstdio>
#include "esp_camera.h"

// GC0308 datasheet pp.10,25: exposure = (P0:03[3:0] << 8) | P0:04;
// automatic exposure is P0:D2 bit7. Do not use the incompatible mappings in
// esp32-camera 2.1.8 set_aec_value()/set_exposure_ctrl(). Units are sensor rows.
class CaptureExposure {
public:
    bool report(const char *stage) {
        if (!read()) return false;
        printf("EXPOSURE stage=%s lines=%d aec=%d mode_d2=0x%02x gain_50=0x%02x gain_70=0x%02x gain_71=0x%02x\n",
               stage, lines_, !!(mode_ & 0x80), mode_, gain50_, gain70_, gain71_);
        return true;
    }

    bool shorten() {
        if (!read()) return false;
        savedMode_ = mode_;
        active_ = true; // Restore even if a following write/read fails.
        if (!write(0xd2, 0xff, savedMode_ & ~0x80)) return fail("disable_aec");
        settle();
        if (!report("frozen") || (mode_ & 0x80)) return fail("freeze_readback");
        savedLines_ = lines_; // Freeze AEC before taking the exposure snapshot.
        haveLines_ = true;
        const int target = savedLines_ / 4 > 0 ? savedLines_ / 4 : 1;
        targetLines_ = target;
        if (!write(0x03, 0x0f, target >> 8) || !write(0x04, 0xff, target & 0xff)) return fail("write_exposure");
        settle();
        if (!report("target_readback") || (mode_ & 0x80) || lines_ != target) return fail("target_readback");
        printf("EXPOSURE stage=shortened baseline_lines=%d target_lines=%d\n", savedLines_, target);
        return report("shortened_readback");
    }

    bool restore() {
        if (!active_) return true;
        bool ok = true;
        if (haveLines_) {
            ok = write(0x03, 0x0f, savedLines_ >> 8);
            ok = write(0x04, 0xff, savedLines_ & 0xff) && ok;
        }
        ok = write(0xd2, 0xff, savedMode_) && ok;
        settle();
        ok = report("restored") && ok;
        ok = mode_ == savedMode_ && ok;
        // Once auto exposure resumes its exposure value may change immediately.
        if (!(savedMode_ & 0x80) && haveLines_) ok = lines_ == savedLines_ && ok;
        if (ok) { active_ = false; haveLines_ = false; }
        return ok;
    }

    int lines() const { return lines_; }
    int aec() const { return !!(mode_ & 0x80); }
    bool captureReady() {
        return report("capture_readback") && (!active_ || (!(mode_ & 0x80) && lines_ == targetLines_));
    }

private:
    int mode_ = 0, lines_ = 0, savedMode_ = 0, savedLines_ = 0, targetLines_ = 0;
    int gain50_ = 0, gain70_ = 0, gain71_ = 0;
    bool active_ = false, haveLines_ = false;

    bool write(int reg, int mask, int value) {
        auto *s = esp_camera_sensor_get();
        return s && s->id.PID == 0x9b && s->set_reg(s, 0xfe, 0xff, 0) == 0 &&
               s->set_reg(s, reg, mask, value) == 0;
    }

    bool read() {
        auto *s = esp_camera_sensor_get();
        if (!s || s->id.PID != 0x9b || s->set_reg(s, 0xfe, 0xff, 0) != 0) return false;
        const int high = s->get_reg(s, 0x03, 0x0f);
        const int low = s->get_reg(s, 0x04, 0xff);
        mode_ = s->get_reg(s, 0xd2, 0xff);
        gain50_ = s->get_reg(s, 0x50, 0xff);
        gain70_ = s->get_reg(s, 0x70, 0xff);
        gain71_ = s->get_reg(s, 0x71, 0xff);
        if (high < 0 || low < 0 || mode_ < 0 || gain50_ < 0 || gain70_ < 0 || gain71_ < 0) return false;
        lines_ = (high << 8) | low;
        return lines_ > 0;
    }

    void settle() {
        // Sensor control registers may latch at frame boundaries. Also drain
        // the queued old frame before interpreting a readback as a failure.
        for (int i = 0; i < 3; ++i) {
            camera_fb_t *fb = esp_camera_fb_get();
            if (fb) esp_camera_fb_return(fb);
        }
    }

    bool fail(const char *stage) {
        printf("EXPOSURE_ERROR stage=%s observed_lines=%d observed_mode=0x%02x target=%d\n",
               stage, lines_, mode_, targetLines_);
        const bool restored = restore();
        printf("EXPOSURE_ERROR restore=%s\n", restored ? "OK" : "FAIL");
        return false;
    }
};
