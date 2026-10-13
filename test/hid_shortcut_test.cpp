#include "hid_shortcut.h"
#include <array>
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstdio>

struct Transport {
    bool connected = true, succeed = true;
    std::vector<std::array<uint8_t, 8>> reports;
    bool ready() { return connected; }
    bool send(const uint8_t (&data)[8]) {
        std::array<uint8_t, 8> report;
        std::copy(data, data + 8, report.begin());
        reports.push_back(report);
        return succeed;
    }
};
bool neutral(const std::array<uint8_t, 8> &r) {
    return std::all_of(r.begin(), r.end(), [](uint8_t b) { return b == 0; });
}
int main() {
    Transport t;
    HidShortcut<Transport> output(t);
    assert(output.trigger(false, 100));
    assert(t.reports.size() == 2 && neutral(t.reports[0]));
    assert(t.reports[1][0] == 4 && t.reports[1][2] == 0x50);
    assert(!output.trigger(true, 110)); // No overlapping or delayed shortcut queue.
    output.poll(139); assert(t.reports.size() == 2);
    output.poll(140); assert(neutral(t.reports.back()) && !output.busy());
    assert(output.trigger(true, 200));
    assert(t.reports.back()[0] == 4 && t.reports.back()[2] == 0x4f);
    output.setEnabled(false, 201);
    assert(neutral(t.reports.back()) && !output.busy());
    assert(!output.trigger(false, 250));
    output.setEnabled(true, 300);
    assert(output.trigger(false, UINT32_MAX - 20));
    output.poll(18); assert(output.busy());
    output.poll(19); assert(!output.busy() && neutral(t.reports.back()));
    assert(output.trigger(true, 400));
    t.connected = false; output.poll(410);
    assert(!output.trigger(false, 420) && !output.busy());
    const auto count = t.reports.size();
    t.connected = true; output.poll(500);
    assert(t.reports.size() == count + 1 && neutral(t.reports.back()));
    output.poll(600); assert(t.reports.size() == count + 1); // Nothing replayed.
    t.succeed = false;
    assert(!output.trigger(true, 700)); // Press may have reached the host.
    assert(neutral(t.reports.back()) && output.busy());
    output.poll(701); assert(output.busy()); // Failed releases must be retried.
    t.succeed = true; output.poll(702);
    assert(neutral(t.reports.back()) && !output.busy());
    puts("HID shortcut tests passed");
}
