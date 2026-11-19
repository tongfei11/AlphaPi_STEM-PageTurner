#pragma once
#include <stdint.h>

// Keep the upstream numeric identifiers so existing mode preferences can migrate.
inline bool modeEnabled(uint8_t mode, bool custom, bool automatic) {
    return mode == 0 || mode == 1 || mode == 3 ||
           (mode == 6 && custom) || (mode == 7 && automatic);
}
inline uint8_t nextMode(uint8_t mode, bool custom, bool automatic) {
    const uint8_t modes[] = {0, 1, 3, 6, 7};
    int index = -1;
    for (int i = 0; i < 5; ++i) if (modes[i] == mode) index = i;
    for (int step = 1; step <= 5; ++step) {
        uint8_t candidate = modes[(index + step) % 5];
        if (modeEnabled(candidate, custom, automatic)) return candidate;
    }
    return 0;
}
