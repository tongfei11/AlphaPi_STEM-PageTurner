#include "../ModePolicy.h"
#include <cassert>

int main() {
    assert(nextMode(0, false, false) == 1);
    assert(nextMode(1, false, false) == 3);
    assert(nextMode(3, false, false) == 0);
    assert(nextMode(3, true, false) == 6);
    assert(nextMode(6, true, false) == 0);
    assert(nextMode(3, false, true) == 7);
    assert(nextMode(7, false, true) == 0);
    assert(nextMode(6, true, true) == 7);
    assert(nextMode(7, true, true) == 0);
    // Removed/disabled preferences can never strand the user in a hidden mode.
    for (unsigned mode = 0; mode < 256; ++mode) {
        for (unsigned flags = 0; flags < 4; ++flags) {
            const bool custom = flags & 1, automatic = flags & 2;
            uint8_t current = mode;
            for (int i = 0; i < 6; ++i) {
                current = nextMode(current, custom, automatic);
                assert(modeEnabled(current, custom, automatic));
            }
        }
    }
}
