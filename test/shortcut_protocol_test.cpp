#include "shortcut_protocol.h"
#include <cassert>
#include <cstdio>
#include <string>
using namespace ShortcutProtocol;

static const char *parseText(const char *text, Request &request) {
    Framer frame;
    for (const char *p = text; *p; ++p) assert(frame.feed(*p) == Framer::Result::Pending);
    assert(frame.feed('\n') == Framer::Result::Complete);
    assert(!frame.invalid());
    return parse(frame.line(), request);
}
int main() {
    Request r;
    assert(!parseText("@G1 42 SET 4 80 1 43 1\r", r));
    assert(r.id == 42 && r.action == Action::Set && r.enabled);
    assert(r.left.modifiers == 4 && r.left.usage == 80 && r.right.modifiers == 1 && r.right.usage == 43);
    assert(!parseText("@G1 4294967295 SET 0 0 255 115 0", r));
    assert(!r.enabled && r.left.usage == 0);
    for (const char *text : {"@G1 0 GET", "@G3 1 GET", "@G1 4294967296 GET", "@G1 1 SET 4 80 4 79",
             "@G1 1 SET -1 80 4 79 1", "@G1 1 SET 256 80 4 79 1", "@G1 1 SET 4 0 4 79 1",
             "@G1 1 SET 0 3 4 79 1", "@G1 1 SET 4 80 4 116 1", "@G1 1 SET 4 80 4 79 2",
             "@G1 1 SET 4 80 4 79 1 extra", "@G1 1 GET extra", "@G1 1 UNKNOWN", "@G1 1 GET\r1u"}) {
        assert(parseText(text, r));
    }
    for (const char *op : {"GET", "KEEP", "STOP", "STANDALONE"}) {
        const std::string line = std::string("@G1 7 ") + op;
        assert(!parseText(line.c_str(), r) && r.id == 7);
    }
    assert(!parseText("@G2 8 SET 1 3 L 4 80 RD 1 29 RDL 0 74", r));
    assert(r.version == 2 && r.enabled && r.bindings.count() == 3);
    assert(r.bindings.keys[ShortcutMap::parsePath("RDL")].usage == 74);
    assert(!parseText("@G2 9 SET 0 0", r) && !r.enabled && !r.bindings.count());
    for (const char *text : {"@G2 1 SET 1 1 RR 0 80", "@G2 1 SET 1 1 RDLU 0 80",
            "@G2 1 SET 1 1 rd 0 80", "@G2 1 SET 1 2 RD 0 0 RD 1 29",
            "@G2 1 SET 1 1 RD 4 0", "@G2 1 SET 1 1 RD 0 116",
            "@G2 1 SET 2 0", "@G2 1 SET 1 53", "@G2 1 SET 1 1 RD 0",
            "@G2 1 SET 1 0 extra", "@G2 1 GET extra"}) assert(parseText(text, r));
    std::string full = "@G2 4294967295 SET 1 52";
    unsigned paths = 0;
    for (unsigned path = 1; path < ShortcutMap::Slots; ++path) {
        if (!ShortcutMap::validPath(path)) continue;
        char text[4]; ShortcutMap::pathText(path, text);
        assert(ShortcutMap::parsePath(text) == path);
        ++paths; full += std::string(" ") + text + " 255 115";
    }
    assert(paths == 52 && !parseText(full.c_str(), r) && r.bindings.count() == 52);
    // A v1 replacement clears all sequence mappings.
    assert(!parseText("@G1 1 SET 4 80 4 79 1", r) && r.bindings.count() == 2);
    Framer f;
    assert(f.feed('i') == Framer::Result::Legacy);
    // Incomplete and oversized commands must never execute legacy 1/u/[ bytes.
    assert(f.feed('@') == Framer::Result::Pending);
    for (int i = 0; i < 1500; ++i) assert(f.feed("1u["[i % 3]) == Framer::Result::Pending);
    assert(f.feed('\n') == Framer::Result::Complete && f.invalid());
    assert(f.feed('0') == Framer::Result::Legacy);
    assert(f.feed('@') == Framer::Result::Pending);
    assert(f.feed('\0') == Framer::Result::Pending);
    assert(f.feed('u') == Framer::Result::Pending);
    assert(f.feed('\n') == Framer::Result::Complete && f.invalid());
    assert(f.feed('@') == Framer::Result::Pending);
    assert(f.feed('\n') == Framer::Result::Complete && !f.invalid());
    puts("Shortcut protocol tests passed");
}
