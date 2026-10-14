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
    for (const char *text : {"@G1 0 GET", "@G2 1 GET", "@G1 4294967296 GET", "@G1 1 SET 4 80 4 79",
             "@G1 1 SET -1 80 4 79 1", "@G1 1 SET 256 80 4 79 1", "@G1 1 SET 4 0 4 79 1",
             "@G1 1 SET 0 3 4 79 1", "@G1 1 SET 4 80 4 116 1", "@G1 1 SET 4 80 4 79 2",
             "@G1 1 SET 4 80 4 79 1 extra", "@G1 1 GET extra", "@G1 1 UNKNOWN", "@G1 1 GET\r1u"}) {
        assert(parseText(text, r));
    }
    for (const char *op : {"GET", "KEEP", "STOP", "STANDALONE"}) {
        const std::string line = std::string("@G1 7 ") + op;
        assert(!parseText(line.c_str(), r) && r.id == 7);
    }
    Framer f;
    assert(f.feed('i') == Framer::Result::Legacy);
    // Incomplete and oversized commands must never execute legacy 1/u/[ bytes.
    assert(f.feed('@') == Framer::Result::Pending);
    for (int i = 0; i < 500; ++i) assert(f.feed("1u["[i % 3]) == Framer::Result::Pending);
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
