// =============================================================================
//  radio.cpp  -  implementation of radio.h
// =============================================================================
#include "radio.h"

volatile bool g_rxFlag = false;
char g_radioErr[48] = {0};

void IRAM_ATTR cardsatOnDio1() { g_rxFlag = true; }

static void setErr(const char* what, int st)
{
    snprintf(g_radioErr, sizeof(g_radioErr), "%s err %d", what, st);
}

bool radioApply(const Config& cfg)
{
    g_radioErr[0] = 0;
    int st;

    // NOTE on hardware: `radio` is whatever RadioLib class LilyGoLib declares for
    // the selected Board Revision (SX1262, LR1121, etc.). The LR1121 LoRa API is
    // method-compatible with the SX1262 for everything below, so this code works
    // on both. Two LR1121-specific points are handled here:
    //  - setFrequency is called BEFORE setOutputPower (the LR11x0 requires power
    //    to be (re)set after a band change), which the ordering below preserves.
    //  - Sync word: see the setSyncWord call below.

    st = radio.setFrequency(cfg.freqMHz());
    if (st != RADIOLIB_ERR_NONE) { setErr("freq", st); return false; }

    st = radio.setBandwidth(cfg.bwKHz());
    if (st != RADIOLIB_ERR_NONE) { setErr("bw", st); return false; }

    st = radio.setSpreadingFactor(cfg.sf);
    if (st != RADIOLIB_ERR_NONE) { setErr("sf", st); return false; }

    st = radio.setCodingRate(cfg.cr);
    if (st != RADIOLIB_ERR_NONE) { setErr("cr", st); return false; }

    // Sync word 0x12 matches CardSat's SX1262. RadioLib maps the single-byte
    // LoRa sync word to the same on-air value across SX126x and LR11x0 families,
    // so 0x12 here should interoperate with the Cardputer's 0x12 — BUT this is
    // the one cross-chip detail worth bench-confirming between the two units.
    st = radio.setSyncWord(cfg.syncWord);
    if (st != RADIOLIB_ERR_NONE) { setErr("sync", st); return false; }

    // Power AFTER frequency (required by LR11x0 on band changes; harmless on SX126x).
    // IMPORTANT (LR1121): setOutputPower takes a second arg selecting the PA path
    // — false = sub-GHz PA, true = 2.4 GHz PA. For 906.875 MHz we MUST pass false,
    // or the wrong PA is used and TX radiates ~60 dB low (the unit transmits but
    // is effectively unheard). The SX126x setOutputPower has no such argument, so
    // the call is compiled per-chip via the board macro.
#if defined(ARDUINO_LILYGO_LORA_LR1121)
    const bool highFreqPA = (cfg.freqKHz >= 2400000);  // sub-GHz -> false
    st = radio.setOutputPower(cfg.txDbm, highFreqPA);
#else
    st = radio.setOutputPower(cfg.txDbm);
#endif
    if (st != RADIOLIB_ERR_NONE) { setErr("pwr", st); return false; }

    // Pin the protocol §2.1 "RadioLib default" items explicitly:
    st = radio.setPreambleLength(8);
    if (st != RADIOLIB_ERR_NONE) { setErr("preamble", st); return false; }

    // CRC length in BYTES. CardSat's SX1262 uses RadioLib's 2-byte LoRa CRC
    // default, so we must set 2 here — not setCRC(true), which would select a
    // 1-byte CRC and make the two radios silently reject each other's packets.
    // On LR11x0 the argument is the CRC byte-length; 2 matches the SX126x default.
    st = radio.setCRC(2);
    if (st != RADIOLIB_ERR_NONE) { setErr("crc", st); return false; }

    // Explicit (variable-length) header is RadioLib's default LoRa header mode;
    // we leave it default, which keeps us interoperable with CardSat.

    // Re-arm the RX-done interrupt (cleared by a full reset inside setters).
    // NOTE: the LR1121 names this setIrqAction(); the SX126x family calls it
    // setDio1Action(). Both register the same RX-done callback on DIO1.
    radio.setIrqAction(cardsatOnDio1);
    return true;
}

void radioStartReceive()
{
    int st = radio.startReceive();
    if (st != RADIOLIB_ERR_NONE) setErr("rx", st);
}

bool radioSend(const char* myCall, const char* text)
{
    uint8_t frame[cardsat::FRAME_MAX];
    size_t  n = cardsat::buildFrame(frame, myCall, text);
    Serial.printf("[tx] sending %u bytes...\n", (unsigned)n);
    int st = radio.transmit(frame, n);
    if (st != RADIOLIB_ERR_NONE) {
        setErr("tx", st);
        Serial.printf("[tx] FAILED, RadioLib code %d\n", st);
        return false;
    }
    Serial.println(F("[tx] transmit() returned OK"));
    return true;
}

bool radioReceive(cardsat::Frame& out, float& rssi, float& snr)
{
    out.valid = false;
    size_t len = radio.getPacketLength();
    if (len == 0) return false;
    if (len > cardsat::FRAME_MAX) len = cardsat::FRAME_MAX;

    uint8_t buf[cardsat::FRAME_MAX];
    int st = radio.readData(buf, len);
    if (st == RADIOLIB_ERR_CRC_MISMATCH) { setErr("crc-rx", st); return false; }
    if (st != RADIOLIB_ERR_NONE)         { setErr("read", st);   return false; }

    rssi = radio.getRSSI();
    snr  = radio.getSNR();

    cardsat::parseFrame(buf, len, out);
    return out.valid;
}
