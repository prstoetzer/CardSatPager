// =============================================================================
//  radio.h  -  Thin wrapper around the LilyGoLib SX1262 handle (instance.radio).
//
//  Centralises every RadioLib call so the rest of the app never touches the
//  radio directly. Applies a Config (protocol §2/§3), transmits CardSat frames,
//  and parses received ones. Pins the three "RadioLib default" items explicitly
//  (preamble 8, CRC on, explicit header) so we don't depend on library defaults.
//
//  Depends on LilyGoLib being initialised (instance.begin()) BEFORE radioApply()
//  is called — instance.radio is the RadioLib SX1262 object for this board.
// =============================================================================
#ifndef CARDSAT_RADIO_H
#define CARDSAT_RADIO_H

#include <LilyGoLib.h>
#include "cardsat_proto.h"
#include "config.h"

// Set by the DIO1 ISR; polled in the main loop.
extern volatile bool g_rxFlag;
void IRAM_ATTR cardsatOnDio1();

// Last error string for the UI to surface (empty = ok).
extern char g_radioErr[48];

// Apply all radio parameters from cfg. Returns true if every setter succeeded.
// Safe to call at runtime to change frequency/SF/BW/etc. live.
bool radioApply(const Config& cfg);

// Begin continuous receive (call after begin and after every TX).
void radioStartReceive();

// Transmit a CardSat frame for (myCall, text). BLOCKS for the air time
// (seconds at SF12). Returns true on success. Caller should radioStartReceive()
// afterwards to return to listening.
bool radioSend(const char* myCall, const char* text);

// Read+parse a pending RX packet into `out`. Returns true if a valid CardSat
// frame was received (out.valid). Fills rssi/snr from the local radio.
bool radioReceive(cardsat::Frame& out, float& rssi, float& snr);

#endif // CARDSAT_RADIO_H
