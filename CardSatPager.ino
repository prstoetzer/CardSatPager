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
#include <LV_Helper.h>     // beginLvglHelper(LilyGo_Display&) — LVGL bring-up
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

static void applyBrightness()
{
    // The Pager backlight starts at 0 after begin(); incrementalBrightness ramps
    // from the current level up to the target (0..16 scale) and drives the real
    // AW9364 LED driver. Using the ramp (rather than a bare setBrightness) is the
    // library's own boot pattern and is the reliable way to light the panel.
    instance.incrementalBrightness(cfg.brightness);
}

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
//  Verified against LV_Helper.h:
//    void beginLvglHelper(LilyGo_Display &display, bool debug = false);
//  `instance` (the global LilyGoLoRaPager) IS a LilyGo_Display, so it binds to
//  the reference parameter directly. The helper registers the ST7796 as an
//  lv_display and the keyboard+encoder+touch as grouped lv_indev objects, so the
//  focused widget receives typed characters and encoder rotation.
//
//  IMPORTANT: beginLvglHelper() calls lv_init() ITSELF — do NOT call lv_init()
//  separately, or LVGL is double-initialised.
// ---------------------------------------------------------------------------
//  Keyboard symbol-layer fix.
//  LilyGoLib's stock Pager keyboard config ships with has_symbol_key = false,
//  which half-disables the orange symbol key: pressing it toggles the symbol
//  layer but returns 0 ("not handled"), so the press falls through to a junk
//  lookup and the layer toggle never takes effect — you can't reach digits or
//  symbols. Re-initialising the keyboard with has_symbol_key = true makes the
//  symbol key behave: press orange, then the top row types 1234567890, the next
//  row * / + - = : ' " @, etc. Keymaps are copied verbatim from LilyGoLib so the
//  per-key mapping is identical. KB_INT / KB_BACKLIGHT come from the board
//  variant (defined by ARDUINO_T_LORA_PAGER).
// ---------------------------------------------------------------------------
static const char kbKeymap[4][10] = {
    {'q','w','e','r','t','y','u','i','o','p'},
    {'a','s','d','f','g','h','j','k','l','\n'},
    {'\0','z','x','c','v','b','n','m','\0','\0'},
    {' ','\0','\0','\0','\0','\0','\0','\0','\0','\0'}
};
static const char kbSymbolMap[4][10] = {
    {'1','2','3','4','5','6','7','8','9','0'},
    {'*','/','+','-','=',':','\'','"','@','\0'},
    {'\0','_','$',';','?','!',',','.','\0','\0'},
    {' ','\0','\0','\0','\0','\0','\0','\0','\0','\0'}
};

// ---------------------------------------------------------------------------
//  Keyboard keycode diagnostic.
//  Set to 1, reflash, open Serial Monitor, and press keys: each prints its raw
//  TCA8418 keycode. Press the orange symbol key and the number-row keys to learn
//  their real codes. With this enabled, NORMAL typing is suppressed (the raw
//  callback short-circuits LilyGoLib's decode), so set it back to 0 when done.
// ---------------------------------------------------------------------------
#define CARDSAT_KB_KEYCODE_DEBUG 0

#if CARDSAT_KB_KEYCODE_DEBUG
static void kbRawProbe(bool pressed, uint8_t raw)
{
    Serial.printf("[kbraw] keycode=0x%02X (%u)  %s\n",
                  raw, raw, pressed ? "DOWN" : "up");
}
#endif

static void fixKeyboardSymbolKey()
{
    // IMPORTANT: kb.begin() stores a POINTER to this config (it does not copy it),
    // so the struct must outlive the call — hence 'static'. A local here would
    // dangle the moment this function returns and corrupt all key decoding.
    static LilyGoKeyboardConfigure_t kc = {};
    kc.kb_rows            = 4;
    kc.kb_cols            = 10;
    kc.current_keymap     = &kbKeymap[0][0];
    kc.current_symbol_map = &kbSymbolMap[0][0];
    // The orange symbol key reports raw keycode 0x15; update() decrements by 1
    // before matching, so its effective value is 0x14. LilyGoLib's stock config
    // wrongly assigned 0x14 to ALT and 0x1E to symbol, so the orange key acted as
    // ALT and symbol mode was unreachable. Assign the orange key (0x14) as the
    // symbol key, and park ALT on an unused code so it can't collide.
    kc.symbol_key_value   = 0x14;   // orange triangle key (raw 0x15, -1 => 0x14)
    kc.alt_key_value      = 0xFE;   // unused; we don't use ALT
    kc.caps_key_value     = 0x1C;
    kc.caps_b_key_value   = 0xFF;
    kc.char_b_value       = 0xFD;   // unused (ALT+B brightness shortcut disabled)
    kc.backspace_value    = 0x1D;
    kc.has_symbol_key     = true;   // <-- the fix (stock config has this false)
    instance.kb.setPins(KB_BACKLIGHT);
    instance.kb.begin(kc, Wire, KB_INT);
    // Pin the keyboard backlight at a fixed PWM level. Left unset it floats at a
    // low duty that can visibly beat/pulse (especially as the display backlight
    // steps down on dim); setting it explicitly holds it steady.
    instance.kb.setBrightness(64);   // 0..255 PWM; steady mid-level

#if CARDSAT_KB_KEYCODE_DEBUG
    instance.kb.setRawCallback(kbRawProbe);   // prints every keycode; suppresses typing
#endif
}

// ---------------------------------------------------------------------------
static void bridgeLvgl()
{
    beginLvglHelper(instance);
}

// ---------------------------------------------------------------------------
//  SETUP
// ---------------------------------------------------------------------------
void setup()
{
    Serial.begin(115200);

    // Brings up PMU, display + backlight, I2C keyboard, encoder, SD, and radio.
    instance.begin();

    // Re-init the keyboard so the orange symbol key reaches digits/symbols
    // (LilyGoLib's stock config disables it). Must come after instance.begin().
    fixKeyboardSymbolKey();

    // Load persisted config (callsign, radio params, UI/power) from NVS.
    configLoad(cfg);

    // LVGL up first so the UI exists, then build screens.
    // NOTE: beginLvglHelper() calls lv_init() internally — don't call it here.
    bridgeLvgl();
    uiInit(&cfg, &store);
    uiSetCallbacks(onSend, onSettingsApplied);

    // Apply backlight AFTER LVGL + the first screen exist (matches the LilyGoLib
    // examples, which draw then setBrightness). Pager scale is 0..16.
    applyBrightness();

    // Configure the radio from cfg and start listening.
    if (!radioApply(cfg)) {
        Serial.printf("[radio] apply failed: %s\n", g_radioErr);
    }
    radioStartReceive();

    char s[48]; radioSummary(s, sizeof(s));
    uiSetRadioStatus(s);

    lastInputMs = millis();

    // Pump LVGL a few times so the first frame actually renders before we enter
    // the main loop (where radio servicing and naps could otherwise delay it).
    for (int i = 0; i < 5; i++) { lv_timer_handler(); delay(10); }

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
        instance.setBrightness(2);   // dim but visible (0..16 scale)
        dimmed = true;
    }

    // Optional deep sleep. Verified signature (LilyGo_LoRa_Pager.h):
    //   void sleep(WakeupSource_t wakeup_src = WAKEUP_SRC_BOOT_BUTTON,
    //              uint32_t sleep_second = 0);
    // Wake on the boot button (and you can OR in the rotary centre button per the
    // header docs). Execution resumes via reset on wake.
    if (cfg.sleepTimeoutS && idleMs > (uint32_t)cfg.sleepTimeoutS * 1000) {
        Serial.println(F("[power] entering deep sleep"));
        instance.sleep(WAKEUP_SRC_BOOT_BUTTON);
    }
}

// Idle yield. We deliberately do NOT use ESP32 light sleep here: entering and
// exiting light sleep cycles the peripheral power domain that the keyboard
// backlight LED sits on, so napping every loop made the keyboard backlight
// visibly pulse. A 20 ms light-sleep nap saves little anyway — the dimmed
// display backlight is the real power saving. So when idle we just delay, which
// keeps the SX1262/LR1121 in continuous receive and the keyboard backlight
// steady. (The cfg.lightSleep toggle is retained for the deep-sleep path.)
static void idleNap()
{
    delay(dimmed ? 15 : 5);
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
    if (millis() - lastStatus > 5000) {
        lastStatus = millis();
        // Battery via the BQ27220 fuel gauge (I2C 0x55). instance.begin() already
        // calls gauge.begin()+setNewCapacity(), so we just refresh and read.
        // getStateOfCharge() is a 0..100% value; a stale/error read can come back
        // >100 (e.g. 0xFFFF), which we treat as "no reading" rather than a bogus
        // 100%. Charging is detected from the signed current: >0 mA = charging.
        if (instance.gauge.refresh()) {
            uint16_t raw = instance.gauge.getStateOfCharge();
            int16_t  cur = instance.gauge.getCurrent();   // mA, +charge / -discharge
            bool charging = (cur > 0);
            Serial.printf("[batt] SOC=%u%%  V=%umV  I=%dmA\n",
                          raw, instance.gauge.getVoltage(), cur);
            if (raw <= 100) {
                uiSetBattery((int)raw, charging);
            } else {
                uiSetBattery(-1, false);   // out-of-range -> "--%"
            }
        } else {
            uiSetBattery(-1, false);   // gauge read failed -> "--%"
        }
        uiTick();
    }

    // 5) Power policy + idle nap.
    powerTick();
    idleNap();
}
