#pragma once
#include "gesture_sequence.h"

// Only a completed path can produce one shortcut. A profile change during the
// gesture invalidates it through the HID generation; segments never send keys.
class SequenceShortcut {
public:
    template<class Output>
    void update(const GestureSequence::Result &result, Output &output) {
        using Event = GestureSequence::Event;
        if (result.event == Event::Segment && result.count == 1) {
            generation_ = output.generation();
            tracking_ = true;
        }
        if (result.event == Event::Cancel) tracking_ = false;
        if (result.event != Event::Complete) return;
        if (tracking_) {
            uint8_t path = 0;
            for (int i = 0; i < result.count; ++i)
                path = path * 5 + static_cast<uint8_t>(result.parts[i]);
            output.emitPath(path, generation_);
        }
        tracking_ = false;
    }
    void reset() { tracking_ = false; }
private:
    uint32_t generation_ = 0;
    bool tracking_ = false;
};
