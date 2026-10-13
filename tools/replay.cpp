// Feed lines "milliseconds grayscale_hex" on stdin. Argument "y" selects axis Y.
#include "gesture_detector.h"
#include "gesture_output.h"
#include <iostream>
#include <string>

int main(int argc, char **argv) {
    GestureDetector detector;
    detector.setVerticalAxis(argc > 1 && std::string(argv[1]) == "y");
    uint32_t now;
    uint32_t previous = 0;
    bool havePrevious = false;
    std::string hex;
    uint8_t image[GestureDetector::Pixels];
    while (std::cin >> now >> hex) {
        if (hex.size() != GestureDetector::Pixels * 2) return 1;
        for (int i = 0; i < GestureDetector::Pixels; ++i) {
            auto digit = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return -1;
            };
            const int high = digit(hex[2 * i]), low = digit(hex[2 * i + 1]);
            if (high < 0 || low < 0) return 1;
            image[i] = static_cast<uint8_t>(high * 16 + low);
        }
        const auto result = detector.process(image, now);
        const char *command = gestureCommand(result.direction, true);
        std::cout << now << ',' << static_cast<int>(result.state) << ',' << result.inliers
                  << ',' << result.movingPatches << ',' << result.alongFlow << ',' << result.acrossFlow
                  << ',' << result.travel << ',' << (command ? command : "")
                  << ',' << (havePrevious ? now - previous : 0)
                  << ',' << result.globalMotion << '\n';
        previous = now;
        havePrevious = true;
    }
}
