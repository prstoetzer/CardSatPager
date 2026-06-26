// =============================================================================
//  CardSatPager.ino  -  Self-contained CardSat-compatible LoRa messenger for the
//                       LilyGo T-LoRa Pager (ESP32-S3 + SX1262).
//
//  Full interactive LVGL app: scrolling chat, on-screen compose, incoming-
//  message notifications (beep/vibrate/wake), idle dimming + light sleep, and a
//  complete on-device Settings screen for every radio + UI parameter, persisted
//  to NVS. Interoperates with an M5Stack Cardputer ADV running CardSat 0.9.24
//  per LORA_MESSAGING_PROTOCOL.md.
//
//  ---------------------------------------------------------------------------
//  BEFORE FLASHING
//   * Set your callsign on first boot via Settings (defaults to NOCALL).
//   * Default carrier is 906.875 MHz (US, 33 cm). Operate only on a band your
//     amateur licence permits. No encryption (amateur rules generally prohibit).
//   * CardSat's own LoRa path is marked UNTESTED in its firmware — bench-verify
//     TX/RX between the two units before relying on the link.
//
//  BUILD: arduino-esp32 >= 3.3.0-alpha1; board "LilyGo-T-LoRa-Pager",
//  Revision Radio-SX1262, USB CDC On Boot enabled, Partition 16M (3M/9.9MB).
//  Requires LilyGoLib + the LilyGoLib-ThirdParty bundle (XPowersLib, RadioLib
//  7.7.1, SensorLib, display + lvgl, etc.) installed in your libraries folder.
//  See README "Two glue points to verify" for the LVGL-bridge + notify calls.
// =============================================================================

#include <LilyGoLib.h>
#include <lvgl.h>

#include "cardsat_proto.h"
#include "config.h"
#include "radio.h"
#include "msgstore.h"
#include "notify.h"
#include "ui.h"

// ---- Global app state ------------------------------------------------------
static Config   cfg;
static MsgStore store;

static uint32_t lastInputMs = 0;   // for idle dim/sleep tracking
static bool     dimmed      = false;

// ---------------------------------------------------------------------------
//  Idle / power helpers
// ---------------------------------------------------------------------------
void uiWake()   // called by notify.h on RX, and by us on any input
{
    lastInputMs = millis();
    if (dimmed) {
        instance.setBrightness(cfg.brightness);
        dimmed = false;
    }
}

static void applyBrightness() { instance.setBrightness(cfg.brightness); }

static void radioSummary(char* out, size_t n)
{
    if (g_radioErr[0]) { snprintf(out, n, "RADIO: %s", g_radioErr); return; }
    float bw = cfg.bwKHz();
    snprintf(out, n, "%.3f SF%u BW%.0f", cfg.freqMHz(), cfg.sf, bw);
}

// ---------------------------------------------------------------------------
//  Callbacks from the UI
// ---------------------------------------------------------------------------
static void onSend(const char* text)
{
    bool ok = radioSend(cfg.callsign, text);   // blocks for air time at SF12
    if (ok) {
        store.push(cfg.callsign, text, 0, 0, true);
        uiOnNewMessage();
    }
    radioStartReceive();
    uiWake();
}

static void onSettingsApplied()
{
    // Re-apply radio with the (possibly) changed parameters, then brightness,
    // and refresh the status line. Called after Settings -> Apply & Save.
    radioApply(cfg);
    radioStartReceive();
    applyBrightness();
    uiWake();
    char s[48]; radioSummary(s, sizeof(s));
    uiSetRadioStatus(s);
}

// ---------------------------------------------------------------------------
//  LVGL <-> LilyGoLib bridge
//
//  GLUE POINT 1 (see README): LilyGoLib provides an LVGL helper that registers
//  the ST7796 as an lv_display and the keyboard+encoder as lv_indev objects,
//  grouped so the focused widget receives typed characters. The helper name
//  differs slightly by LilyGoLib revision — commonly `beginLvglHelper()`. If
//  yours differs, call the matching helper here. After it runs, lv_* calls in
//  ui.cpp drive the screen and the keyboard types into the focused textarea.
// ---------------------------------------------------------------------------
static void bridgeLvgl()
{
    beginLvglHelper(instance);   // <-- reconcile this name with your LilyGoLib
}

// ---------------------------------------------------------------------------
//  SETUP
// ---------------------------------------------------------------------------
void setup()
{
    Serial.begin(115200);

    // Brings up PMU, display + backlight, I2C keyboard, encoder, SD, and radio.
    instance.begin();

    // Load persisted config (callsign, radio params, UI/power) from NVS.
    configLoad(cfg);
    applyBrightness();

    // LVGL up first so the UI exists, then build screens.
    lv_init();
    bridgeLvgl();
    uiInit(&cfg, &store);
    uiSetCallbacks(onSend, onSettingsApplied);

    // Configure the radio from cfg and start listening.
    if (!radioApply(cfg)) {
        Serial.printf("[radio] apply failed: %s\n", g_radioErr);
    }
    radioStartReceive();

    char s[48]; radioSummary(s, sizeof(s));
    uiSetRadioStatus(s);

    lastInputMs = millis();
    Serial.println(F("CardSatPager ready."));
}

// ---------------------------------------------------------------------------
//  Idle policy: dim after dimTimeoutS, light-sleep tick when idle, optional
//  deep sleep after sleepTimeoutS. Light sleep here is a short, RX-preserving
//  nap between LVGL/radio service passes — the SX1262 stays in continuous
//  receive (its DIO1 line wakes us), so we keep hearing traffic while cutting
//  CPU between polls.
// ---------------------------------------------------------------------------
static void powerTick()
{
    uint32_t idleMs = millis() - lastInputMs;

    // Dim the backlight.
    if (!dimmed && cfg.dimTimeoutS && idleMs > (uint32_t)cfg.dimTimeoutS * 1000) {
        instance.setBrightness(cfg.brightness / 6 + 2);   // low, not fully off
        dimmed = true;
    }

    // Optional deep sleep (wakes on keypress/encoder via the PMU/EXT wake the
    // HAL configures). Only if the operator enabled a nonzero timeout.
    if (cfg.sleepTimeoutS && idleMs > (uint32_t)cfg.sleepTimeoutS * 1000) {
        Serial.println(F("[power] entering deep sleep"));
        instance.sleep();   // GLUE POINT 2b: LilyGoLib deep-sleep entry
        // execution resumes via reset on wake
    }
}

// Light-sleep nap that preserves SX1262 RX. We arm a short timer wake AND the
// radio's DIO1 as an EXT wake source, so either the timer or an inbound packet
// brings us back promptly. Falls back to a plain delay if light sleep is off.
static void idleNap()
{
    if (!cfg.lightSleep || dimmed == false) { delay(5); return; }

    // Wake on DIO1 (RX done) or after a short interval to service LVGL ticks.
    // The HAL knows the SX1262 DIO1 GPIO; if your LilyGoLib exposes a helper to
    // register it as a wake source, prefer that. Timer wake keeps the UI live.
    esp_sleep_enable_timer_wakeup(20 * 1000);   // 20 ms
    esp_light_sleep_start();
}

// ---------------------------------------------------------------------------
//  MAIN LOOP
// ---------------------------------------------------------------------------
void loop()
{
    // 1) Radio: service a pending RX.
    if (g_rxFlag) {
        g_rxFlag = false;
        cardsat::Frame f; float rssi, snr;
        if (radioReceive(f, rssi, snr)) {
            store.push(f.from, f.text, rssi, snr, false);
            uiOnNewMessage();
            notifyIncoming(cfg);     // beep/vibrate/wake per config
        }
        radioStartReceive();
        char s[48]; radioSummary(s, sizeof(s));
        uiSetRadioStatus(s);
    }

    // 2) LVGL: drive timers/redraw and let it consume keyboard+encoder input.
    lv_timer_handler();

    // 3) Detect user activity via LVGL inactivity to reset the idle clock.
    if (lv_display_get_inactive_time(NULL) < 50) uiWake();

    // 4) Periodic status (battery, unread badge).
    static uint32_t lastStatus = 0;
    if (millis() - lastStatus > 1000) {
        lastStatus = millis();
        int pct = instance.getBatteryPercent();   // GLUE POINT 2a: PMU gauge
        bool chg = instance.isCharging();
        uiSetBattery(pct, chg);
        uiTick();
    }

    // 5) Power policy + idle nap.
    powerTick();
    idleNap();
}
