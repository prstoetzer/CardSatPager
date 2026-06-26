// =============================================================================
//  ui.cpp  -  LVGL UI implementation.
//
//  Layout (480x222 landscape):
//    +--------------------------------------------------+
//    | status bar: callsign | radio summary | batt |[N] |
//    +--------------------------------------------------+
//    | chat list (scrolling message bubbles)            |
//    |                                                  |
//    +--------------------------------------------------+
//    | [ compose textarea .................. ] [Send]   |
//    +--------------------------------------------------+
//  A gear button in the status bar opens the Settings screen, which has an
//  editable widget for every Config field and an Apply/Save button.
//
//  Input: the keyboard + encoder are registered with LVGL as an indev group by
//  the main sketch (see README "LVGL input wiring"). Typing into the focused
//  textarea works once that group is attached.
// =============================================================================
#include "ui.h"
#include <stdio.h>

static Config*   s_cfg   = nullptr;
static MsgStore* s_store = nullptr;
static UiSendCb               s_onSend = nullptr;
static UiSettingsAppliedCb    s_onApplied = nullptr;

// --- Screen / widget handles ---
static lv_obj_t* scr_chat;
static lv_obj_t* scr_settings;
static lv_obj_t* lbl_call;
static lv_obj_t* lbl_radio;
static lv_obj_t* lbl_batt;
static lv_obj_t* lbl_badge;
static lv_obj_t* list_chat;
static lv_obj_t* ta_compose;

// settings widgets
static lv_obj_t* set_call;
static lv_obj_t* set_region;
static lv_obj_t* set_freq;
static lv_obj_t* set_sf;
static lv_obj_t* set_bw;
static lv_obj_t* set_cr;
static lv_obj_t* set_sync;
static lv_obj_t* set_pwr;
static lv_obj_t* set_bri;
static lv_obj_t* set_dim;
static lv_obj_t* set_sleep;
static lv_obj_t* set_snd;
static lv_obj_t* set_vib;
static lv_obj_t* set_lsl;

// ---------------------------------------------------------------------------
//  Chat list
// ---------------------------------------------------------------------------
static void addBubble(const StoredMsg& m)
{
    lv_obj_t* cont = lv_obj_create(list_chat);
    lv_obj_set_width(cont, lv_pct(96));
    lv_obj_set_height(cont, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(cont, 4, 0);
    lv_obj_set_style_margin_ver(cont, 2, 0);
    lv_obj_set_style_radius(cont, 6, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    if (m.mine) {
        lv_obj_set_style_bg_color(cont, lv_palette_darken(LV_PALETTE_GREEN, 3), 0);
        lv_obj_align(cont, LV_ALIGN_RIGHT_MID, 0, 0);
    } else {
        lv_obj_set_style_bg_color(cont, lv_palette_darken(LV_PALETTE_BLUE_GREY, 2), 0);
    }

    lv_obj_t* lbl = lv_label_create(cont);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, lv_pct(100));

    char line[160];
    if (m.mine) {
        snprintf(line, sizeof(line), "%s\n#a0ffa0 %s#", m.text, m.from);
    } else {
        snprintf(line, sizeof(line), "#80c0ff %s#  [%.0f/%.0f]\n%s",
                 m.from[0] ? m.from : "?", m.rssi, m.snr, m.text);
    }
    lv_label_set_recolor(lbl, true);
    lv_label_set_text(lbl, line);

    lv_obj_scroll_to_view(cont, LV_ANIM_ON);
}

void uiOnNewMessage()
{
    if (s_store->count() == 0) return;
    addBubble(s_store->at(s_store->count() - 1));
}

// ---------------------------------------------------------------------------
//  Send button
// ---------------------------------------------------------------------------
static void onSendClicked(lv_event_t* e)
{
    const char* txt = lv_textarea_get_text(ta_compose);
    if (txt && txt[0] && s_onSend) {
        s_onSend(txt);
        lv_textarea_set_text(ta_compose, "");
    }
}

// ---------------------------------------------------------------------------
//  Settings: open / populate / apply
// ---------------------------------------------------------------------------
static void fillSettingsFromCfg()
{
    char buf[24];
    lv_textarea_set_text(set_call, s_cfg->callsign);

    lv_dropdown_set_selected(set_region,
        s_cfg->region == REGION_US ? 0 :
        s_cfg->region == REGION_EU ? 1 :
        s_cfg->region == REGION_JP ? 2 : 3);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)s_cfg->freqKHz);
    lv_textarea_set_text(set_freq, buf);

    lv_dropdown_set_selected(set_sf, s_cfg->sf - 7);                    // 7..12 -> 0..5
    lv_dropdown_set_selected(set_bw,
        s_cfg->bwHz_div == 62 ? 0 : s_cfg->bwHz_div == 125 ? 1 : 2);
    lv_dropdown_set_selected(set_cr, s_cfg->cr - 5);                    // 5..8 -> 0..3

    snprintf(buf, sizeof(buf), "%u", s_cfg->syncWord);
    lv_textarea_set_text(set_sync, buf);

    lv_slider_set_value(set_pwr,   s_cfg->txDbm,        LV_ANIM_OFF);
    lv_slider_set_value(set_bri,   s_cfg->brightness,   LV_ANIM_OFF);
    lv_slider_set_value(set_dim,   s_cfg->dimTimeoutS,  LV_ANIM_OFF);
    lv_slider_set_value(set_sleep, s_cfg->sleepTimeoutS,LV_ANIM_OFF);

    if (s_cfg->soundOnRx)   lv_obj_add_state(set_snd, LV_STATE_CHECKED); else lv_obj_clear_state(set_snd, LV_STATE_CHECKED);
    if (s_cfg->vibrateOnRx) lv_obj_add_state(set_vib, LV_STATE_CHECKED); else lv_obj_clear_state(set_vib, LV_STATE_CHECKED);
    if (s_cfg->lightSleep)  lv_obj_add_state(set_lsl, LV_STATE_CHECKED); else lv_obj_clear_state(set_lsl, LV_STATE_CHECKED);
}

static void onRegionChanged(lv_event_t* e)
{
    // When a preset is picked, push its freq/bw into the freq field for visibility.
    uint16_t sel = lv_dropdown_get_selected(set_region);
    uint8_t region = (sel == 0) ? REGION_US : (sel == 1) ? REGION_EU :
                     (sel == 2) ? REGION_JP : REGION_CUSTOM;
    if (region != REGION_CUSTOM) {
        Config tmp = *s_cfg;
        configApplyRegion(tmp, region);
        char buf[24]; snprintf(buf, sizeof(buf), "%lu", (unsigned long)tmp.freqKHz);
        lv_textarea_set_text(set_freq, buf);
        lv_dropdown_set_selected(set_bw, tmp.bwHz_div == 62 ? 0 : tmp.bwHz_div == 125 ? 1 : 2);
    }
}

static void onSettingsApply(lv_event_t* e)
{
    Config c = *s_cfg;

    strncpy(c.callsign, lv_textarea_get_text(set_call), cardsat::FROM_LEN);
    c.callsign[cardsat::FROM_LEN] = 0;

    uint16_t rsel = lv_dropdown_get_selected(set_region);
    c.region = (rsel == 0) ? REGION_US : (rsel == 1) ? REGION_EU :
               (rsel == 2) ? REGION_JP : REGION_CUSTOM;

    c.freqKHz = strtoul(lv_textarea_get_text(set_freq), nullptr, 10);
    c.sf = 7 + lv_dropdown_get_selected(set_sf);
    uint16_t bwsel = lv_dropdown_get_selected(set_bw);
    c.bwHz_div = (bwsel == 0) ? 62 : (bwsel == 1) ? 125 : 250;
    c.cr = 5 + lv_dropdown_get_selected(set_cr);
    c.syncWord = (uint8_t)strtoul(lv_textarea_get_text(set_sync), nullptr, 0);
    c.txDbm = lv_slider_get_value(set_pwr);
    c.brightness = lv_slider_get_value(set_bri);
    c.dimTimeoutS = lv_slider_get_value(set_dim);
    c.sleepTimeoutS = lv_slider_get_value(set_sleep);
    c.soundOnRx   = lv_obj_has_state(set_snd, LV_STATE_CHECKED);
    c.vibrateOnRx = lv_obj_has_state(set_vib, LV_STATE_CHECKED);
    c.lightSleep  = lv_obj_has_state(set_lsl, LV_STATE_CHECKED);

    configClamp(c);
    *s_cfg = c;
    configSave(c);                 // persist to NVS
    if (s_onApplied) s_onApplied();   // app re-applies radio + brightness

    lv_screen_load(scr_chat);      // back to chat
}

static void onOpenSettings(lv_event_t* e)
{
    fillSettingsFromCfg();
    lv_screen_load(scr_settings);
}

static void onSettingsBack(lv_event_t* e) { lv_screen_load(scr_chat); }

// Helper: labelled row in the settings flow layout.
static lv_obj_t* settingsRow(lv_obj_t* parent, const char* label)
{
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(row, 2, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* l = lv_label_create(row);
    lv_label_set_text(l, label);
    return row;
}

static void buildSettingsScreen()
{
    scr_settings = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr_settings, lv_color_black(), 0);

    lv_obj_t* col = lv_obj_create(scr_settings);
    lv_obj_set_size(col, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(col, 6, 0);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);

    lv_obj_t* title = lv_label_create(col);
    lv_label_set_text(title, "Settings");

    lv_obj_t* row;
    row = settingsRow(col, "Callsign");
    set_call = lv_textarea_create(row);
    lv_textarea_set_one_line(set_call, true);
    lv_textarea_set_max_length(set_call, cardsat::FROM_LEN);
    lv_obj_set_width(set_call, 140);

    row = settingsRow(col, "Region");
    set_region = lv_dropdown_create(row);
    lv_dropdown_set_options(set_region, "US 906.875\nEU 433.775\nJP 431.000\nCustom");
    lv_obj_add_event_cb(set_region, onRegionChanged, LV_EVENT_VALUE_CHANGED, nullptr);

    row = settingsRow(col, "Freq (kHz)");
    set_freq = lv_textarea_create(row);
    lv_textarea_set_one_line(set_freq, true);
    lv_textarea_set_accepted_chars(set_freq, "0123456789");
    lv_textarea_set_max_length(set_freq, 7);
    lv_obj_set_width(set_freq, 110);

    row = settingsRow(col, "Spreading factor");
    set_sf = lv_dropdown_create(row);
    lv_dropdown_set_options(set_sf, "7\n8\n9\n10\n11\n12");

    row = settingsRow(col, "Bandwidth (kHz)");
    set_bw = lv_dropdown_create(row);
    lv_dropdown_set_options(set_bw, "62.5\n125\n250");

    row = settingsRow(col, "Coding rate 4/");
    set_cr = lv_dropdown_create(row);
    lv_dropdown_set_options(set_cr, "5\n6\n7\n8");

    row = settingsRow(col, "Sync word (dec)");
    set_sync = lv_textarea_create(row);
    lv_textarea_set_one_line(set_sync, true);
    lv_textarea_set_accepted_chars(set_sync, "0123456789");
    lv_textarea_set_max_length(set_sync, 3);
    lv_obj_set_width(set_sync, 70);

    row = settingsRow(col, "TX power (dBm)");
    set_pwr = lv_slider_create(row);
    lv_slider_set_range(set_pwr, 0, 22);
    lv_obj_set_width(set_pwr, 150);

    row = settingsRow(col, "Brightness");
    set_bri = lv_slider_create(row);
    lv_slider_set_range(set_bri, 0, 255);
    lv_obj_set_width(set_bri, 150);

    row = settingsRow(col, "Dim after (s, 0=off)");
    set_dim = lv_slider_create(row);
    lv_slider_set_range(set_dim, 0, 300);
    lv_obj_set_width(set_dim, 150);

    row = settingsRow(col, "Sleep after (s, 0=off)");
    set_sleep = lv_slider_create(row);
    lv_slider_set_range(set_sleep, 0, 1800);
    lv_obj_set_width(set_sleep, 150);

    row = settingsRow(col, "Beep on RX");
    set_snd = lv_switch_create(row);
    row = settingsRow(col, "Vibrate on RX");
    set_vib = lv_switch_create(row);
    row = settingsRow(col, "Light sleep when idle");
    set_lsl = lv_switch_create(row);

    // buttons
    lv_obj_t* btns = lv_obj_create(col);
    lv_obj_set_width(btns, lv_pct(100));
    lv_obj_set_height(btns, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btns, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btns, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(btns, LV_OPA_TRANSP, 0);

    lv_obj_t* back = lv_button_create(btns);
    lv_label_set_text(lv_label_create(back), "Back");
    lv_obj_add_event_cb(back, onSettingsBack, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* apply = lv_button_create(btns);
    lv_label_set_text(lv_label_create(apply), "Apply & Save");
    lv_obj_add_event_cb(apply, onSettingsApply, LV_EVENT_CLICKED, nullptr);
}

// ---------------------------------------------------------------------------
//  Chat screen
// ---------------------------------------------------------------------------
static void buildChatScreen()
{
    scr_chat = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr_chat, lv_color_black(), 0);
    lv_obj_set_flex_flow(scr_chat, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(scr_chat, 0, 0);

    // status bar
    lv_obj_t* bar = lv_obj_create(scr_chat);
    lv_obj_set_width(bar, lv_pct(100));
    lv_obj_set_height(bar, 24);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(bar, 6, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lbl_call  = lv_label_create(bar); lv_label_set_text(lbl_call, "NOCALL");
    lbl_radio = lv_label_create(bar); lv_label_set_text(lbl_radio, "--");
    lbl_batt  = lv_label_create(bar); lv_label_set_text(lbl_batt, "--%");
    lbl_badge = lv_label_create(bar); lv_label_set_text(lbl_badge, "");

    lv_obj_t* gear = lv_button_create(bar);
    lv_label_set_text(lv_label_create(gear), LV_SYMBOL_SETTINGS);
    lv_obj_add_event_cb(gear, onOpenSettings, LV_EVENT_CLICKED, nullptr);

    // chat list (grows)
    list_chat = lv_obj_create(scr_chat);
    lv_obj_set_width(list_chat, lv_pct(100));
    lv_obj_set_flex_grow(list_chat, 1);
    lv_obj_set_flex_flow(list_chat, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(list_chat, 4, 0);
    lv_obj_set_style_bg_opa(list_chat, LV_OPA_TRANSP, 0);

    // compose bar
    lv_obj_t* compose = lv_obj_create(scr_chat);
    lv_obj_set_width(compose, lv_pct(100));
    lv_obj_set_height(compose, 36);
    lv_obj_set_flex_flow(compose, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(compose, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(compose, 4, 0);
    lv_obj_clear_flag(compose, LV_OBJ_FLAG_SCROLLABLE);

    ta_compose = lv_textarea_create(compose);
    lv_textarea_set_one_line(ta_compose, true);
    lv_textarea_set_max_length(ta_compose, cardsat::TEXT_MAX);  // clamp 48 (§4.2)
    lv_textarea_set_placeholder_text(ta_compose, "Message...");
    lv_obj_set_flex_grow(ta_compose, 1);

    lv_obj_t* send = lv_button_create(compose);
    lv_label_set_text(lv_label_create(send), LV_SYMBOL_UP);
    lv_obj_add_event_cb(send, onSendClicked, LV_EVENT_CLICKED, nullptr);
}

// ---------------------------------------------------------------------------
//  Public API
// ---------------------------------------------------------------------------
void uiInit(Config* cfg, MsgStore* store)
{
    s_cfg = cfg; s_store = store;
    buildChatScreen();
    buildSettingsScreen();
    lv_label_set_text(lbl_call, cfg->callsign);
    lv_screen_load(scr_chat);
}

void uiSetCallbacks(UiSendCb onSend, UiSettingsAppliedCb onApplied)
{
    s_onSend = onSend; s_onApplied = onApplied;
}

void uiSetBattery(int percent, bool charging)
{
    char b[16];
    snprintf(b, sizeof(b), "%s%d%%", charging ? LV_SYMBOL_CHARGE : "", percent);
    lv_label_set_text(lbl_batt, b);
}

void uiSetRadioStatus(const char* s) { lv_label_set_text(lbl_radio, s); }

void uiTick()
{
    int u = s_store->unread();
    if (u > 0) { char b[8]; snprintf(b, sizeof(b), LV_SYMBOL_BELL "%d", u); lv_label_set_text(lbl_badge, b); }
    else       { lv_label_set_text(lbl_badge, ""); }
    lv_label_set_text(lbl_call, s_cfg->callsign);
}
