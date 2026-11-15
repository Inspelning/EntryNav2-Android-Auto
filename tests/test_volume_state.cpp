// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// What the volume pill shows: the knob's level on a change or after a key, never another source's, and mute.
#include <stdio.h>
#include <stdlib.h>

#include "volume_state.hpp"

#define CHECK(x)                                                 \
    do {                                                         \
        if (!(x)) {                                              \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); \
            exit(1);                                             \
        }                                                        \
    } while (0)

namespace {

evo::VolumeLevel level(unsigned value, unsigned min, unsigned max) {
    evo::VolumeLevel l = {value, min, max};
    return l;
}

bool shows(const evo::VolumeState::Shown& s, unsigned value, unsigned min, unsigned max) {
    return s.show && s.level.value == value && s.level.min == min && s.level.max == max;
}

void atClock(uint32_t t) {
    evo::VolumeState v;
    CHECK(!v.known() && !v.muted());
    // The MINI F57 at connection: another source's level, again and again, while the knob's volume is 3.
    for (int i = 0; i < 10; ++i) {
        CHECK(!v.level(level(25, 25, 100), t + 300 * i).show);
    }
    CHECK(!v.known());
    // The first level of the knob's volume shows; the same one again, nothing having changed, does not.
    CHECK(shows(v.level(level(3, 0, 100), t + 4000), 3, 0, 100));
    CHECK(!v.level(level(3, 0, 100), t + 4300).show);
    // A change shows.
    CHECK(shows(v.level(level(5, 0, 100), t + 5000), 5, 0, 100));
    // A volume key: the next level shows even unchanged (the knob at its end stop), not one past the window.
    v.volumeKey(t + 6000);
    CHECK(shows(v.level(level(5, 0, 100), t + 6100), 5, 0, 100));
    CHECK(!v.level(level(5, 0, 100), t + 6000 + evo::VolumeState::kKeyWindowMs + 1).show);
    // The audio knob: muted, the speaker crossed out at the minimum; the level kept for later.
    CHECK(shows(v.audioButton(t + 7000), 0, 0, 100) && v.muted());
    CHECK(shows(v.level(level(5, 0, 100), t + 7100), 0, 0, 100));  // within the button's window: still muted
    // Pressed again: the sound back, at its level.
    CHECK(shows(v.audioButton(t + 8000), 5, 0, 100) && !v.muted());
    // A volume step ends a mute, as iDrive does.
    v.audioButton(t + 9000);
    CHECK(v.muted());
    v.volumeKey(t + 9500);
    CHECK(!v.muted() && shows(v.level(level(6, 0, 100), t + 9600), 6, 0, 100));
    // The button before any level: muted, nothing to show yet.
    evo::VolumeState fresh;
    CHECK(!fresh.audioButton(t).show && fresh.muted());
    CHECK(shows(fresh.level(level(8, 0, 40), t + 100), 0, 0, 40));
}

}  // namespace

int main() {
    atClock(1000);
    atClock(0xFFFFF000u);  // across the 32-bit wrap of the millisecond clock
    atClock(0);            // a key at time 0 still counts
    puts("PASS volume state: another source's level ignored, repeats held back, keys, mute by the audio knob");
    return 0;
}
