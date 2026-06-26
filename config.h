// =============================================================================
//  config.h  -  All on-device-configurable parameters, persisted to NVS.
//
//  Every field here is editable from the on-device Settings screen and survives
//  reboot (stored via ESP32 Preferences / NVS). Defaults are the CardSat US
//  preset. Values are validated/clamped on load and on set so a bad stored value
//  can never push the radio out of a legal/working range.
// =============================================================================
#ifndef CARDSAT_CONFIG_H
#define CARDSAT_CONFIG_H

#include <Arduino.h>
#include <Preferences.h>
#include "cardsat_proto.h"

// Region presets (protocol §3). Frequencies in kHz.
enum Region : uint8_t { REGION_US = 0, REGION_EU = 1, REGION_JP = 2, REGION_CUSTOM = 255 };

struct Config {
    // --- Identity ---
    char     callsign[cardsat::FROM_LEN + 1] = "NOCALL";

    // --- Radio (protocol §2/§3). Frequency stored in kHz like CardSat. ---
    uint32_t freqKHz   = 906875;   // US default 906.875 MHz
    uint8_t  sf        = 12;       // 7..12
    uint16_t bwHz_div  = 125;      // 62 (=62.5), 125, 250  (kHz; 62 means 62.5)
    uint8_t  cr        = 5;        // coding rate denom 4/CR; CardSat fixes 4/5
    uint8_t  syncWord  = 0x12;     // CardSat private sync word
    int8_t   txDbm     = 20;       // 0..22
    uint8_t  region    = REGION_US;

    // --- UI / power ---
    uint8_t  brightness      = 12;    // 0..16 backlight (Pager scale; 16 = max)
    uint16_t dimTimeoutS     = 30;    // idle seconds -> dim screen (0 = never)
    uint16_t sleepTimeoutS   = 0;     // idle seconds -> deep sleep (0 = never)
    bool     soundOnRx       = true;  // buzzer/beep on incoming message
    bool     vibrateOnRx     = false; // motor pulse on incoming (if fitted)
    bool     lightSleep      = true;  // light-sleep between RX polls when idle

    // Convenience: real bandwidth in kHz as a float for RadioLib.
    float bwKHz() const { return (bwHz_div == 62) ? 62.5f : (float)bwHz_div; }
    float freqMHz() const { return freqKHz / 1000.0f; }
};

// ---- Validation / clamping -------------------------------------------------
inline void configClamp(Config& c)
{
    c.callsign[cardsat::FROM_LEN] = 0;
    if (c.sf < 7)  c.sf = 7;
    if (c.sf > 12) c.sf = 12;
    if (c.bwHz_div != 62 && c.bwHz_div != 125 && c.bwHz_div != 250) c.bwHz_div = 125;
    if (c.cr < 5)  c.cr = 5;            // CardSat uses 4/5; keep >=5 to be safe
    if (c.cr > 8)  c.cr = 8;
    if (c.txDbm < 0)  c.txDbm = 0;
    if (c.txDbm > 22) c.txDbm = 22;
    // Frequency: SX1262 legal range 150..960 MHz. Keep within the chip's range;
    // the OPERATOR is responsible for staying on a band their licence permits.
    if (c.freqKHz < 150000) c.freqKHz = 150000;
    if (c.freqKHz > 960000) c.freqKHz = 960000;
    if (c.region > REGION_JP && c.region != REGION_CUSTOM) c.region = REGION_CUSTOM;
    // Brightness is the Pager's 0..16 scale (DEVICE_MAX_BRIGHTNESS_LEVEL=16).
    // Clamp to 1..16 so a stale 0..255 NVS value or a zero can't black out the
    // screen. Minimum 1 keeps the panel visible.
    if (c.brightness < 1)  c.brightness = 1;
    if (c.brightness > 16) c.brightness = 16;
}

// Apply a region preset's freq + bw (protocol §3). Leaves SF/power/etc alone.
inline void configApplyRegion(Config& c, uint8_t region)
{
    switch (region) {
        case REGION_US: c.freqKHz = 906875; c.bwHz_div = 125; c.region = REGION_US; break;
        case REGION_EU: c.freqKHz = 433775; c.bwHz_div = 125; c.region = REGION_EU; break;
        case REGION_JP: c.freqKHz = 431000; c.bwHz_div = 125; c.region = REGION_JP; break;
        default: c.region = REGION_CUSTOM; break;
    }
    configClamp(c);
}

// ---- Persistence (NVS namespace "cardsat") ---------------------------------
inline void configLoad(Config& c)
{
    Preferences p;
    p.begin("cardsat", true);   // read-only
    if (p.isKey("cs")) {
        p.getBytes("cs", c.callsign, sizeof(c.callsign));
        c.freqKHz       = p.getULong("freq",  c.freqKHz);
        c.sf            = p.getUChar("sf",     c.sf);
        c.bwHz_div      = p.getUShort("bw",    c.bwHz_div);
        c.cr            = p.getUChar("cr",     c.cr);
        c.syncWord      = p.getUChar("sync",   c.syncWord);
        c.txDbm         = (int8_t)p.getChar("tx", c.txDbm);
        c.region        = p.getUChar("rgn",    c.region);
        c.brightness    = p.getUChar("bri",    c.brightness);
        c.dimTimeoutS   = p.getUShort("dim",   c.dimTimeoutS);
        c.sleepTimeoutS = p.getUShort("slp",   c.sleepTimeoutS);
        c.soundOnRx     = p.getBool("snd",     c.soundOnRx);
        c.vibrateOnRx   = p.getBool("vib",     c.vibrateOnRx);
        c.lightSleep    = p.getBool("lsl",     c.lightSleep);
    }
    p.end();
    configClamp(c);
}

inline void configSave(const Config& cin)
{
    Config c = cin;
    configClamp(c);
    Preferences p;
    p.begin("cardsat", false);  // read-write
    p.putBytes("cs", c.callsign, sizeof(c.callsign));
    p.putULong("freq",  c.freqKHz);
    p.putUChar("sf",     c.sf);
    p.putUShort("bw",    c.bwHz_div);
    p.putUChar("cr",     c.cr);
    p.putUChar("sync",   c.syncWord);
    p.putChar("tx",      c.txDbm);
    p.putUChar("rgn",    c.region);
    p.putUChar("bri",    c.brightness);
    p.putUShort("dim",   c.dimTimeoutS);
    p.putUShort("slp",   c.sleepTimeoutS);
    p.putBool("snd",     c.soundOnRx);
    p.putBool("vib",     c.vibrateOnRx);
    p.putBool("lsl",     c.lightSleep);
    p.end();
}

#endif // CARDSAT_CONFIG_H
