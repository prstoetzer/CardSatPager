// =============================================================================
//  msgstore.h  -  Message history ring (mirrors CardSat MSG_MAX = 24).
// =============================================================================
#ifndef CARDSAT_MSGSTORE_H
#define CARDSAT_MSGSTORE_H

#include <Arduino.h>
#include "cardsat_proto.h"

struct StoredMsg {
    char     from[cardsat::FROM_LEN + 1];
    char     text[cardsat::TEXT_MAX + 1];
    float    rssi;
    float    snr;
    uint32_t millisStamp;   // local arrival time (CardSat stamps locally too)
    bool     mine;          // true = sent by us
    bool     unread;        // for the notification badge
};

class MsgStore {
public:
    static const int CAP = 24;

    void push(const char* from, const char* text, float rssi, float snr, bool mine) {
        StoredMsg& m = ring_[head_];
        strncpy(m.from, from, cardsat::FROM_LEN); m.from[cardsat::FROM_LEN] = 0;
        strncpy(m.text, text, cardsat::TEXT_MAX); m.text[cardsat::TEXT_MAX] = 0;
        m.rssi = rssi; m.snr = snr; m.millisStamp = millis();
        m.mine = mine; m.unread = !mine;
        head_ = (head_ + 1) % CAP;
        if (count_ < CAP) count_++;
        if (!mine) unreadCount_++;
    }

    int count() const { return count_; }
    int unread() const { return unreadCount_; }
    void markAllRead() {
        unreadCount_ = 0;
        for (int i = 0; i < count_; i++) ring_[i].unread = false;
    }

    // Index 0 = oldest visible, count()-1 = newest.
    const StoredMsg& at(int i) const {
        int start = (head_ - count_ + CAP) % CAP;
        return ring_[(start + i) % CAP];
    }

private:
    StoredMsg ring_[CAP];
    int head_ = 0;
    int count_ = 0;
    int unreadCount_ = 0;
};

#endif // CARDSAT_MSGSTORE_H
