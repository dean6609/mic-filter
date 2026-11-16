#pragma once

namespace micfilter {
// Controls read once per audio callback. Portable: shared by the Windows effect and the
// Linux/macOS terminal app.
struct Settings {
    bool enabled=false;
    float threshold=0.85f;
    unsigned grace=20;
    float wet=1.0f;
    unsigned voice=0;
};
}
