// =============================================================================
//  notify.h  -  Incoming-message notifications: sound + vibration.
//
//  These route through LilyGoLib helpers. The exact method names for the
//  buzzer/haptic on the Pager vary by LilyGoLib revision; the two calls below
//  are isolated here so they're the only spot to reconcile. If your version
//  lacks one, leave the corresponding config toggle off and the call is skipped.
// =============================================================================
#ifndef CARDSAT_NOTIFY_H
#define CARDSAT_NOTIFY_H

#include <LilyGoLib.h>
#include "config.h"

// Wake the screen to full brightness and reset the dim/sleep idle timer.
// Defined in the main sketch (it owns the brightness + idle state).
void uiWake();

inline void notifyIncoming(const Config& cfg)
{
    // Always wake the display so a new message is visible.
    uiWake();

    if (cfg.soundOnRx) {
        // LilyGoLib exposes a short tone helper on boards with a buzzer/speaker.
        // If your revision uses a different call (e.g. instance.playTone / a
        // ToneController), substitute it here.
        #if defined(LILYGO_HAS_BUZZER) || 1
        instance.setWaveform(0, 1);   // placeholder: see README "Notification API"
        #endif
    }

    if (cfg.vibrateOnRx) {
        // Haptic motor, if fitted. Same caveat as above.
        // instance.vibrator(...) / instance.setHapticEffect(...) per revision.
    }
}

#endif // CARDSAT_NOTIFY_H
