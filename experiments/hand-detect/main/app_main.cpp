#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hand_detect.hpp"

namespace {
constexpr int Width = 320, Height = 240;
constexpr char Tag[] = "hand_eval";

esp_err_t start_camera() {
    gpio_config_t power{};
    power.pin_bit_mask = 1ULL << 18;
    power.mode = GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&power));
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_18, 0));
    vTaskDelay(pdMS_TO_TICKS(500));
    camera_config_t c{};
    c.pin_pwdn = -1; c.pin_reset = -1;
    c.pin_xclk = 21; c.pin_sccb_sda = 12; c.pin_sccb_scl = 9;
    c.pin_d0 = 3; c.pin_d1 = 42; c.pin_d2 = 46; c.pin_d3 = 48;
    c.pin_d4 = 4; c.pin_d5 = 17; c.pin_d6 = 11; c.pin_d7 = 13;
    c.pin_vsync = 10; c.pin_href = 14; c.pin_pclk = 40;
    c.xclk_freq_hz = 20000000;
    c.ledc_timer = LEDC_TIMER_0; c.ledc_channel = LEDC_CHANNEL_0;
    c.pixel_format = PIXFORMAT_RGB565;
    c.frame_size = FRAMESIZE_QVGA;
    // One buffer isolates capture from inference for this initial benchmark.
    c.fb_count = 1;
    c.fb_location = CAMERA_FB_IN_PSRAM;
    c.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    return esp_camera_init(&c);
}

void memory(const char *stage) {
    ESP_LOGI(Tag, "MEM stage=%s internal_free=%u psram_free=%u psram_largest=%u", stage,
             unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
             unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
}

void snapshot(const uint8_t *rgb, uint32_t sequence) {
    // Text framing avoids CDC newline conversion corrupting binary pixels.
    constexpr char hex[] = "0123456789abcdef";
    char row[Height * 3 * 2 + 1];
    printf("HAND_IMAGE_BEGIN seq=%lu width=%d height=%d\n", (unsigned long)sequence, Height, Width);
    for (int y = 0; y < Width; ++y) {
        for (int x = 0; x < Height * 3; ++x) {
            const uint8_t pixel = rgb[y * Height * 3 + x];
            row[x * 2] = hex[pixel >> 4]; row[x * 2 + 1] = hex[pixel & 15];
        }
        row[Height * 3 * 2] = '\0';
        printf("HAND_IMAGE_ROW %d %s\n", y, row);
        vTaskDelay(1);
    }
    printf("HAND_IMAGE_END seq=%lu\n", (unsigned long)sequence);
}
} // namespace

extern "C" void app_main() {
    usb_serial_jtag_driver_config_t console{};
    console.rx_buffer_size = 256;
    console.tx_buffer_size = 4096;
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&console));
    usb_serial_jtag_vfs_use_driver();
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
    ESP_LOGI(Tag, "Hand detection evaluation; HID=OFF I2C=OFF; RGB565 QVGA -> RGB888 CW90");
    memory("boot");
    auto *rgb = static_cast<uint8_t *>(heap_caps_malloc(Width * Height * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!rgb) { ESP_LOGE(Tag, "RGB buffer allocation failed"); return; }
    // Smoke test model loading/inference before enabling camera traffic.
    std::memset(rgb, 0, Width * Height * 3);
    HandDetect detect(HandDetect::ESPDET_PICO_224_224_HAND, false);
    dl::image::img_t image{};
    image.data = rgb; image.width = Height; image.height = Width;
    image.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB888;
    const int64_t smoke_start = esp_timer_get_time();
    const auto &smoke = detect.run(image);
    ESP_LOGI(Tag, "MODEL_SMOKE hands=%u elapsed_ms=%.1f", unsigned(smoke.size()),
             (esp_timer_get_time() - smoke_start) / 1000.0);
    memory("model_loaded");
    const esp_err_t result = start_camera();
    if (result != ESP_OK) {
        ESP_LOGE(Tag, "Camera init failed: %s", esp_err_to_name(result));
        heap_caps_free(rgb);
        return;
    }
    ESP_LOGI(Tag, "Camera sensor=0x%02x; warming up", esp_camera_sensor_get()->id.PID);
    const int64_t warmup = esp_timer_get_time();
    while (esp_timer_get_time() - warmup < 2000000) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) esp_camera_fb_return(fb);
        vTaskDelay(1);
    }
    memory("camera_ready");
    ESP_LOGI(Tag, "READY: boxes use rotated image coordinates, width=240 height=320");
    uint32_t sequence = 0, failures = 0;
    int64_t previous_timestamp = -1;
    while (true) {
        char command = 0;
        const bool save_image = read(STDIN_FILENO, &command, 1) == 1 && command == 's';
        const int64_t start = esp_timer_get_time();
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) { ++failures; ESP_LOGE(Tag, "Capture failed count=%lu", (unsigned long)failures); vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        const int64_t timestamp = int64_t(fb->timestamp.tv_sec) * 1000000 + fb->timestamp.tv_usec;
        if (fb->format != PIXFORMAT_RGB565 || fb->width != Width || fb->height != Height ||
            fb->len != Width * Height * 2 || timestamp <= previous_timestamp) {
            esp_camera_fb_return(fb);
            ++failures;
            ESP_LOGE(Tag, "Invalid/stale frame count=%lu", (unsigned long)failures);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        previous_timestamp = timestamp;
        const int64_t captured = esp_timer_get_time();
        // Camera bytes are RGB565 big-endian. Rotate clockwise: physical right
        // corresponds to -sensor Y in the mounting used by the existing firmware.
        for (int y = 0; y < Height; ++y) for (int x = 0; x < Width; ++x) {
            const size_t src = (y * Width + x) * 2;
            const uint16_t pixel = (uint16_t(fb->buf[src]) << 8) | fb->buf[src + 1];
            const size_t dst = (x * Height + Height - 1 - y) * 3;
            rgb[dst] = ((pixel >> 11) & 31) * 255 / 31;
            rgb[dst + 1] = ((pixel >> 5) & 63) * 255 / 63;
            rgb[dst + 2] = (pixel & 31) * 255 / 31;
        }
        esp_camera_fb_return(fb);
        const int64_t converted = esp_timer_get_time();
        const auto &hands = detect.run(image);
        const int64_t finished = esp_timer_get_time();
        printf("HAND_FRAME seq=%lu capture_ms=%.1f convert_ms=%.1f infer_ms=%.1f total_ms=%.1f age_ms=%.1f hands=%u failures=%lu\n",
               (unsigned long)++sequence, (captured-start)/1000.0, (converted-captured)/1000.0,
               (finished-converted)/1000.0, (finished-start)/1000.0, (finished-timestamp)/1000.0,
               unsigned(hands.size()), (unsigned long)failures);
        for (const auto &hand : hands) {
            printf("HAND_BOX seq=%lu score=%.4f x1=%d y1=%d x2=%d y2=%d\n", (unsigned long)sequence,
                   double(hand.score), hand.box[0], hand.box[1], hand.box[2], hand.box[3]);
        }
        if (save_image) snapshot(rgb, sequence);
        if (sequence % 30 == 0) memory("running");
        vTaskDelay(1);
    }
}
