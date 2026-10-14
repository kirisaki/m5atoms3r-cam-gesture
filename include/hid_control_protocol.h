#pragma once
#include "shortcut_protocol.h"

// One vendor Feature report: report ID 6 + 63 payload bytes. All integers LE.
// [0..3] AGH1, [4] op, [5] status, [6..9] request ID, [10] page,
// [11] total bindings, [12] flags, [13] max steps, [14..17] transaction/generation,
// [18..62] 15 triples (base-5 path, modifiers, usage). Unused bytes must be zero.
namespace HidControlProtocol {
constexpr uint8_t ReportId = 6;
constexpr uint16_t UsagePage = 0xff00, Usage = 0x53;
constexpr size_t Size = 63, PageSize = 15;
enum Op : uint8_t { Info = 1, Begin, Page, Commit, Keep, Stop, Standalone };
enum Status : uint8_t { Ok, Format, Range, Path, Expired, Measurement, Unsupported, Changed };
enum Flag : uint8_t { Managed = 1, Enabled = 2, Lease = 4, Locked = 8 };
inline uint32_t read32(const uint8_t *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline void write32(uint8_t *p, uint32_t v) { for (unsigned i = 0; i < 4; ++i) p[i] = v >> (8 * i); }
inline bool zero(const uint8_t *p, size_t count) { while (count--) if (*p++) return false; return true; }
inline Status errorCode(const char *error) {
    if (!error) return Ok;
    if (!std::strcmp(error, "MEASUREMENT")) return Measurement;
    if (!std::strcmp(error, "EXPIRED")) return Expired;
    if (!std::strcmp(error, "UNSUPPORTED_PATH")) return Unsupported;
    return Format;
}

class Server {
public:
    // Called only from the camera loop, never the USB callback/task.
    template<class Target>
    void handle(const uint8_t (&input)[Size], uint8_t (&output)[Size], Target &target,
                bool measurement, uint8_t maxSteps, uint32_t now) {
        uint8_t page = input[10];
        const Status status = execute(input, target, measurement, now);
        if (status != Ok || input[4] != Info) page = 0;
        std::memset(output, 0, Size);
        std::memcpy(output, "AGH1", 4);
        output[4] = input[4]; output[5] = status;
        std::memcpy(output + 6, input + 6, 4);
        const auto &state = target.state();
        output[10] = page;
        output[11] = state.bindings().count();
        output[12] = (state.managed() ? Managed : 0) | (state.enabled() ? Enabled : 0) |
                     (state.leaseActive() ? Lease : 0) | (measurement ? Locked : 0);
        output[13] = maxSteps;
        write32(output + 14, state.generation());
        unsigned index = 0;
        for (unsigned path = 1; path < ShortcutMap::Slots; ++path) {
            const auto key = state.bindings().keys[path];
            if (!key.usage) continue;
            const unsigned position = index++;
            if (position < page * PageSize || position >= (page + 1) * PageSize) continue;
            const unsigned offset = 18 + 3 * (position - page * PageSize);
            output[offset] = path; output[offset + 1] = key.modifiers; output[offset + 2] = key.usage;
        }
    }
private:
    ShortcutMap staged_;
    uint32_t transaction_ = 0, started_ = 0, generation_ = 0;
    uint8_t count_ = 0, received_ = 0, page_ = 0;
    bool active_ = false, enabled_ = false;
    template<class Target>
    Status execute(const uint8_t (&p)[Size], Target &target, bool measurement, uint32_t now) {
        const uint8_t op = p[4];
        const uint32_t transaction = read32(p + 14);
        if (std::memcmp(p, "AGH1", 4) || !read32(p + 6) || p[5] || p[13] || op < Info || op > Standalone) {
            active_ = false; return Format;
        }
        if (op != Page && !zero(p + 18, 45)) { active_ = false; return Format; }
        if (op != Begin && p[12]) { active_ = false; return Format; }
        if (op != Page && op != Info && p[10]) { active_ = false; return Format; }
        if (op != Begin && op != Page && p[11]) { active_ = false; return Format; }
        if (op != Begin && op != Page && op != Commit && transaction) { active_ = false; return Format; }
        if (op == Info) {
            const size_t count = target.state().bindings().count();
            return p[10] && p[10] * PageSize >= count ? Range : Ok;
        }
        if (op == Begin) {
            active_ = false;
            if (measurement) return Measurement;
            if (!transaction || p[11] > ShortcutMap::MaxBindings || (p[12] & ~Enabled)) return Range;
            staged_ = ShortcutMap{}; received_ = page_ = 0;
            count_ = p[11]; enabled_ = p[12] & Enabled;
            transaction_ = transaction; started_ = now; generation_ = target.state().generation();
            active_ = true; return Ok;
        }
        if (op == Page || op == Commit) {
            if (!active_ || transaction != transaction_ || now - started_ >= 1500) {
                active_ = false; return Expired;
            }
            if (target.state().generation() != generation_) { active_ = false; return Changed; }
            if (op == Page) {
                if (p[10] != page_ || p[11] != count_ || received_ >= count_) { active_ = false; return Range; }
                const unsigned remaining = count_ - received_;
                const unsigned amount = remaining < PageSize ? remaining : PageSize;
                if (!zero(p + 18 + 3 * amount, 45 - 3 * amount)) { active_ = false; return Format; }
                for (unsigned i = 0; i < amount; ++i) {
                    const auto *entry = p + 18 + 3 * i;
                    if (!ShortcutMap::validPath(entry[0]) || staged_.keys[entry[0]].usage) { active_ = false; return Path; }
                    if (!entry[2] || !ShortcutProtocol::validBinding(entry[1], entry[2])) { active_ = false; return Range; }
                    staged_.keys[entry[0]] = {entry[1], entry[2]};
                }
                received_ += amount; ++page_; return Ok;
            }
            active_ = false;
            if (received_ != count_) return Range;
            ShortcutProtocol::Request request;
            request.action = ShortcutProtocol::Action::Set;
            request.bindings = staged_; request.enabled = enabled_;
            return errorCode(target.apply(request, measurement));
        }
        ShortcutProtocol::Request request;
        if (op == Keep) request.action = ShortcutProtocol::Action::Keep;
        else {
            active_ = false;
            request.action = op == Stop ? ShortcutProtocol::Action::Stop : ShortcutProtocol::Action::Standalone;
        }
        return errorCode(target.apply(request, measurement));
    }
};
} // namespace HidControlProtocol
