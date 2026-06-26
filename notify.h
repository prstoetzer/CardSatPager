// =============================================================================
//  notify.h  -  Incoming-message notifications: haptic + screen wake.
//
//  Verified against LilyGo_LoRa_Pager.h:
//    void vibrator();                 // single buzz of the haptic motor
//    void setHapticEffects(uint8_t);  // select DRV2605-style effect (optional)
//
//  The Pager has NO simple piezo buzzer/tone API on this class — audio runs
//  through the ES8311 codec, which is far heavier than a notification warrants.
//  So the "alert" here is the haptic motor (the natural pager behaviour). The
//  config's soundOnRx and vibrateOnRx are both mapped onto the vibrator: enabling
//  either gives you a buzz. The screen-wake always happens so a new message is
//  visible.
//
//  If you later want an audible chirp, drive the ES8311 with a short tone sample
//  in app.cpp; it's intentionally left out of the notification path to keep RX
//  servicing light.
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

    // Haptic alert for either "sound" or "vibrate" preference. `instance` is the
    // global LilyGoLoRaPager object from LilyGoLib.h.
    if (cfg.soundOnRx || cfg.vibrateOnRx) {
        instance.vibrator();   // one short buzz
    }
}

#endif // CARDSAT_NOTIFY_H
