// =============================================================================
//  cardsat_proto.h  -  CardSat 0.9.24 on-air frame: constants + build/parse.
//
//  This file is deliberately HAL-INDEPENDENT and has no Arduino/LilyGo/RadioLib
//  dependency beyond <stdint.h>/<string.h>. It is the byte-exact ground truth
//  from LORA_MESSAGING_PROTOCOL.md §4. Lift it into any client unchanged.
//
//  Frame:
//    [0]      0xC5  magic
//    [1]      0x01  version
//    [2..15]  from  : 14-byte ASCII callsign, SPACE-padded, no NUL on air
//    [16..]   text  : 0..48 bytes ASCII, length = packetLen - 16, no NUL on air
//  Total 16..64 bytes.
// =============================================================================
#ifndef CARDSAT_PROTO_H
#define CARDSAT_PROTO_H

#include <stdint.h>
#include <string.h>

namespace cardsat {

static const uint8_t MSG_MAGIC = 0xC5;   // byte 0
static const uint8_t MSG_VER   = 0x01;   // byte 1
static const uint32_t FROM_LEN  = 14;    // callsign field width (bytes 2..15)
static const uint32_t TEXT_MAX  = 48;    // max text length (bytes 16..63)
static const uint32_t FRAME_MAX = 2 + FROM_LEN + TEXT_MAX;  // 64
static const uint32_t FRAME_MIN = 2 + FROM_LEN;            // 16 (empty text)

// Build an on-air frame into `out` (must be >= FRAME_MAX bytes).
// `myCall` is space-padded to 14 bytes; empty -> "NOCALL". `text` clamped to 48
// bytes and NOT null-terminated on air. Returns the frame length (16..64).
inline size_t buildFrame(uint8_t* out, const char* myCall, const char* text)
{
    size_t n = 0;
    out[n++] = MSG_MAGIC;
    out[n++] = MSG_VER;

    const char* call = (myCall && myCall[0]) ? myCall : "NOCALL";
    size_t calllen = strlen(call);
    for (size_t i = 0; i < FROM_LEN; i++) {
        out[n++] = (i < calllen) ? (uint8_t)call[i] : (uint8_t)0x20;  // space pad
    }

    size_t tlen = text ? strlen(text) : 0;
    if (tlen > TEXT_MAX) tlen = TEXT_MAX;             // clamp 48 (§4.2)
    if (tlen) memcpy(out + n, text, tlen);
    n += tlen;
    return n;                                         // 16..64
}

// Parsed view of a received frame.
struct Frame {
    bool valid;                  // passed magic+version acceptance check (§4.3)
    char from[FROM_LEN + 1];     // trimmed callsign, NUL-terminated
    char text[TEXT_MAX + 1];     // text, NUL-terminated
};

// Parse `buf`/`len` defensively per §4.3 / §7. Always fills `out`; sets
// out.valid=false (and empty fields) if the packet is not a CardSat frame.
inline void parseFrame(const uint8_t* buf, size_t len, Frame& out)
{
    out.valid = false;
    out.from[0] = 0;
    out.text[0] = 0;

    // Acceptance: discard unless len>=2 && buf[0]==0xC5 && buf[1]==0x01.
    if (len < 2 || buf[0] != MSG_MAGIC || buf[1] != MSG_VER) return;
    out.valid = true;

    // Field lengths derived from arrival, clamped to field widths.
    size_t fromLen = (len > 2) ? (len - 2) : 0;
    if (fromLen > FROM_LEN) fromLen = FROM_LEN;
    size_t textLen = (len > 16) ? (len - 16) : 0;
    if (textLen > TEXT_MAX) textLen = TEXT_MAX;

    memcpy(out.from, buf + 2, fromLen);
    out.from[fromLen] = 0;
    // trim trailing spaces to recover callsign (§4.1)
    for (int i = (int)fromLen - 1; i >= 0 && out.from[i] == ' '; i--) out.from[i] = 0;

    if (textLen) memcpy(out.text, buf + 16, textLen);
    out.text[textLen] = 0;
}

} // namespace cardsat
#endif // CARDSAT_PROTO_H
