#include "hid_control_protocol.h"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace HidControlProtocol;
struct Transport { bool ready() { return true; } bool send(const uint8_t (&)[8]) { return true; } };
struct Target {
    Transport transport;
    HidShortcut<Transport> shortcut{transport};
    uint32_t now = 0;
    unsigned sets = 0;
    bool legacy = false;
    const HidShortcut<Transport> &state() const { return shortcut; }
    const char *apply(const ShortcutProtocol::Request &r, bool measurement) {
        using ShortcutProtocol::Action;
        switch (r.action) {
            case Action::Set:
                if (measurement) return "MEASUREMENT";
                if (legacy) for (size_t i = 3; i < ShortcutMap::Slots; ++i) if (r.bindings.keys[i].usage) return "UNSUPPORTED_PATH";
                shortcut.configure(r.bindings, r.enabled, now); ++sets; break;
            case Action::Stop: shortcut.stop(now); break;
            case Action::Keep: if (!shortcut.keepAlive(now)) return "EXPIRED"; break;
            case Action::Standalone: if (measurement) return "MEASUREMENT"; shortcut.standalone(now); break;
            case Action::Get: break;
        }
        return nullptr;
    }
};
struct Packet { uint8_t data[Size] = {}; };
Packet request(Op op, uint32_t transaction = 0, uint8_t page = 0, uint8_t count = 0, uint8_t flags = 0) {
    Packet p; std::memcpy(p.data, "AGH1", 4); p.data[4] = op; write32(p.data + 6, 7);
    p.data[10] = page; p.data[11] = count; p.data[12] = flags; write32(p.data + 14, transaction); return p;
}
struct Trial {
    Server server; Target target; bool measurement = false;
    Packet run(Packet input, Status expected = Ok) {
        target.shortcut.poll(target.now);
        Packet output; server.handle(input.data, output.data, target, measurement, target.legacy ? 1 : 3, target.now);
        assert(output.data[5] == expected); assert(read32(output.data + 6) == 7); return output;
    }
};
int main() {
    Trial t;
    const auto initial = t.run(request(Info));
    const uint8_t golden[] = {'A','G','H','1',1,0,7,0,0,0,0,2,2,3,0,0,0,0,1,4,80,2,4,79};
    assert(!std::memcmp(initial.data, golden, sizeof(golden)));
    assert(zero(initial.data + sizeof(golden), Size - sizeof(golden)));
    std::vector<uint8_t> paths;
    for (unsigned i = 1; i < ShortcutMap::Slots; ++i) if (ShortcutMap::validPath(i)) paths.push_back(i);
    assert(paths.size() == 52);
    t.run(request(Begin, 42, 0, 52, Enabled));
    for (unsigned page = 0; page < 4; ++page) {
        auto p = request(Page, 42, page, 52);
        for (unsigned i = 0; i < PageSize && page * PageSize + i < paths.size(); ++i) {
            p.data[18 + 3*i] = paths[page * PageSize + i]; p.data[19 + 3*i] = 255; p.data[20 + 3*i] = 115;
        }
        t.run(p); assert(!t.target.sets && t.target.state().bindings().count() == 2);
    }
    const auto committed = t.run(request(Commit, 42));
    assert(committed.data[11] == 52 && committed.data[12] == (Managed | Enabled | Lease) && t.target.sets == 1);
    for (unsigned page = 0; page < 4; ++page) {
        const auto response = t.run(request(Info, 0, page));
        assert(response.data[10] == page && read32(response.data + 14) == t.target.state().generation());
        for (unsigned i = 0; i < PageSize && page * PageSize + i < paths.size(); ++i)
            assert(response.data[18 + 3*i] == paths[page * PageSize + i] && response.data[20 + 3*i] == 115);
    }
    t.run(request(Commit, 42), Expired); assert(t.target.sets == 1); // Never replay a commit.
    t.run(request(Info, 0, 4), Range);
    // Incomplete, duplicate, reordered and stale transactions leave live mappings intact.
    t.run(request(Begin, 43, 0, 2)); t.run(request(Commit, 43), Range);
    auto duplicate = request(Page, 44, 0, 2);
    duplicate.data[18] = duplicate.data[21] = 14; duplicate.data[20] = duplicate.data[23] = 74;
    t.run(request(Begin, 44, 0, 2)); t.run(duplicate, Path); t.run(request(Commit, 44), Expired);
    t.run(request(Begin, 45, 0, 2)); t.run(request(Page, 45, 1, 2), Range);
    t.run(request(Begin, 46)); t.run(request(Commit, 45), Expired);
    t.run(request(Begin, 47)); t.target.now = 1500; t.run(request(Commit, 47), Expired);
    assert(t.target.sets == 1 && t.target.state().bindings().count() == 52);
    // Serial STOP/SET, lease expiry, measurement, and invalid requests invalidate staging.
    t.run(request(Begin, 48)); t.target.shortcut.stop(1500); t.run(request(Commit, 48), Changed);
    t.run(request(Begin, 49)); t.run(request(Stop)); t.run(request(Commit, 49), Expired);
    t.measurement = true; t.run(request(Begin, 50), Measurement); t.run(request(Standalone), Measurement);
    t.measurement = false;
    auto bad = request(Begin, 51); bad.data[62] = 1; t.run(bad, Format);
    bad = request(Info); bad.data[0] = 'X'; t.run(bad, Format);
    bad = request(Info); bad.data[12] = 1; t.run(bad, Format);
    bad = request(Info); write32(bad.data + 6, 0);
    Packet response; t.server.handle(bad.data, response.data, t.target, false, 3, 1500); assert(response.data[5] == Format);
    // Empty replacement and timer wraparound.
    t.target.now = UINT32_MAX - 200; t.run(request(Begin, 52)); t.target.now = 100;
    t.run(request(Commit, 52)); assert(t.target.state().bindings().count() == 0 && !t.target.state().enabled());
    t.target.now = 2100; t.run(request(Keep), Expired);
    t.run(request(Stop)); assert(!t.target.state().enabled());
    puts("PASS: HID pages, full map snapshot, atomic commit, malformed/stale transactions, lease/STOP and wraparound");
}
