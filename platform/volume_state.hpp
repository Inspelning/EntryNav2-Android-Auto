// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// What the volume pill shows, decided as iDrive decides its own volume bar: pure, no clock, no lock.
//
// Read in the reference car's HmiMain3D (iDrive 202111011321) and libhmil6services.so:
//  - The level is an array property of the AudioMaster (fkt 0xC08, CPxyAudioMaster::CVisibleVolumeIndicator), one
//    record per display: value, min, max (one byte each, 0 to 100). A Status starts with PosX, PosY: PosX 0 carries
//    the whole array, PosX n the single record n - 1. The front display's record is the first one
//    (CHmiVolumeIndicatorService::copyVolumeIndicatorValuesToStruct, index 0 for this unit's role).
//  - iDrive keeps that record in one of two slots by its minimum: 0, the volume the knob turns; above 0, another
//    source's (seen on a MINI F57 at connection: 25 between 25 and 100, ten times in four seconds, while the knob's
//    volume was 3 between 0 and 100). Only the first slot is the knob's: the other one never shows the pill.
//  - The same level comes again without anything changing; iDrive's bar opens on a change or a volume key, so does
//    the pill.
//  - Mute is iDrive's own: pressing the audio knob (CECenterStack 0x15.0C fkt 0xC04 PushButtonAudio, value 1) toggles
//    it (CDevVolumeControl::onPushButtonAudio, then onMuteTriggered); a volume step ends it. The AudioMaster's level
//    does not change, so the pill shows the speaker crossed out from the button alone.
// tests/test_volume_state.cpp.
#ifndef ENTRY_AAD_PLATFORM_VOLUME_STATE_HPP
#define ENTRY_AAD_PLATFORM_VOLUME_STATE_HPP

#include <stdint.h>

namespace evo {

// One record of the volume level, as the AudioMaster reports it.
struct VolumeLevel {
    unsigned value, min, max;
};

class VolumeState {
public:
    static const uint32_t kKeyWindowMs = 1000;  // a level within this of a volume key or the audio button shows

    // What the pill shows after an event: nothing, or a level (value at min for the crossed-out speaker).
    struct Shown {
        bool show;
        VolumeLevel level;
    };

    VolumeState() : known_(false), muted_(false), keyAt_(0) { level_.value = level_.min = level_.max = 0; }

    // A level of the front display. A level of another source (minimum above 0) is not the knob's.
    Shown level(const VolumeLevel& reported, uint32_t now) {
        if (reported.min != 0) {
            return none();
        }
        const bool changed = !known_ || reported.value != level_.value || reported.max != level_.max;
        level_ = reported;
        known_ = true;
        if (!changed && !recentKey(now)) {
            return none();
        }
        return shown();
    }
    // A volume step (knob or steering wheel): ends the mute; the level that follows shows.
    void volumeKey(uint32_t now) {
        muted_ = false;
        keyAt_ = nz(now);
    }
    // The audio knob pressed: mute on or off, shown at once when the level is known.
    Shown audioButton(uint32_t now) {
        muted_ = !muted_;
        keyAt_ = nz(now);
        return known_ ? shown() : none();
    }

    bool known() const { return known_; }
    bool muted() const { return muted_; }

private:
    static uint32_t nz(uint32_t t) { return t ? t : 1; }                                   // 0 means "no key yet"
    bool recentKey(uint32_t now) const { return keyAt_ && now - keyAt_ <= kKeyWindowMs; }  // wraps with the clock
    static Shown none() {
        Shown s;
        s.show = false;
        s.level.value = s.level.min = s.level.max = 0;
        return s;
    }
    Shown shown() const {
        Shown s;
        s.show = true;
        s.level = level_;
        if (muted_) {
            s.level.value = level_.min;  // the pill crosses the speaker out at its minimum
        }
        return s;
    }

    VolumeLevel level_;
    bool known_, muted_;
    uint32_t keyAt_;
};

}  // namespace evo

#endif  // ENTRY_AAD_PLATFORM_VOLUME_STATE_HPP
