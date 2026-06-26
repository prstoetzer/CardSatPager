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

    st = instance.radio.setFrequency(cfg.freqMHz());
    if (st != RADIOLIB_ERR_NONE) { setErr("freq", st); return false; }

    st = instance.radio.setBandwidth(cfg.bwKHz());
    if (st != RADIOLIB_ERR_NONE) { setErr("bw", st); return false; }

    st = instance.radio.setSpreadingFactor(cfg.sf);
    if (st != RADIOLIB_ERR_NONE) { setErr("sf", st); return false; }

    st = instance.radio.setCodingRate(cfg.cr);
    if (st != RADIOLIB_ERR_NONE) { setErr("cr", st); return false; }

    st = instance.radio.setSyncWord(cfg.syncWord);
    if (st != RADIOLIB_ERR_NONE) { setErr("sync", st); return false; }

    st = instance.radio.setOutputPower(cfg.txDbm);
    if (st != RADIOLIB_ERR_NONE) { setErr("pwr", st); return false; }

    // Pin the protocol §2.1 "RadioLib default" items explicitly:
    st = instance.radio.setPreambleLength(8);
    if (st != RADIOLIB_ERR_NONE) { setErr("preamble", st); return false; }

    st = instance.radio.setCRC(true);
    if (st != RADIOLIB_ERR_NONE) { setErr("crc", st); return false; }

    // Explicit (variable-length) header is RadioLib's default LoRa header mode;
    // we leave it default, which keeps us interoperable with CardSat.

    // Re-arm the RX-done interrupt (cleared by a full reset inside setters).
    instance.radio.setDio1Action(cardsatOnDio1);
    return true;
}

void radioStartReceive()
{
    int st = instance.radio.startReceive();
    if (st != RADIOLIB_ERR_NONE) setErr("rx", st);
}

bool radioSend(const char* myCall, const char* text)
{
    uint8_t frame[cardsat::FRAME_MAX];
    size_t  n = cardsat::buildFrame(frame, myCall, text);
    int st = instance.radio.transmit(frame, n);
    if (st != RADIOLIB_ERR_NONE) { setErr("tx", st); return false; }
    return true;
}

bool radioReceive(cardsat::Frame& out, float& rssi, float& snr)
{
    out.valid = false;
    size_t len = instance.radio.getPacketLength();
    if (len == 0) return false;
    if (len > cardsat::FRAME_MAX) len = cardsat::FRAME_MAX;

    uint8_t buf[cardsat::FRAME_MAX];
    int st = instance.radio.readData(buf, len);
    if (st == RADIOLIB_ERR_CRC_MISMATCH) { setErr("crc-rx", st); return false; }
    if (st != RADIOLIB_ERR_NONE)         { setErr("read", st);   return false; }

    rssi = instance.radio.getRSSI();
    snr  = instance.radio.getSNR();

    cardsat::parseFrame(buf, len, out);
    return out.valid;
}
