// =============================================================================
//  ui.h  -  LVGL user interface: chat view, compose bar, settings screen.
// =============================================================================
#ifndef CARDSAT_UI_H
#define CARDSAT_UI_H

#include <lvgl.h>
#include "config.h"
#include "msgstore.h"

// Called once after LVGL + LilyGoLib are initialised. Builds all screens.
// `cfg` is the live config (settings edits write through to it); `store` is the
// message history to render.
void uiInit(Config* cfg, MsgStore* store);

// Push the newest message into the chat list (call after store->push()).
void uiOnNewMessage();

// Refresh the status bar (battery, unread badge, radio summary).
void uiTick();

// Hooks the app provides to the UI:
//  - when the user presses Send in the compose bar
//  - when the user applies changed settings (so the app can re-apply the radio)
typedef void (*UiSendCb)(const char* text);
typedef void (*UiSettingsAppliedCb)(void);
void uiSetCallbacks(UiSendCb onSend, UiSettingsAppliedCb onSettingsApplied);

// Status-bar data the app updates.
void uiSetBattery(int percent, bool charging);
void uiSetRadioStatus(const char* shortStatus);  // e.g. "906.875 SF12" or an error

#endif // CARDSAT_UI_H
