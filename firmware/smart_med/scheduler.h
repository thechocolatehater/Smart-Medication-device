// scheduler.h - decides which doses are due (Day 3).
// Each dose fires once a day. Doses due in the same minute wait in a
// queue and are announced one after another. Expired schedules never
// fire, and after a restart, doses earlier in the day are not repeated.
#pragma once
#include "records.h"

struct DueDose {
  uint8_t rec;    // record index 0-9 (container rec + 1)
  uint8_t dose;   // which of its doses, 0-3
};

// Dates are packed into 16 bits so they can be compared with < and >.
uint16_t dateKey(uint8_t y, uint8_t m, uint8_t d);   // y = year - 2000
bool     dateValid(uint8_t y, uint8_t m, uint8_t d);
void     nextDay(uint8_t& y, uint8_t& m, uint8_t& d);
bool     recordExpired(const MedRecord& r, uint16_t today);

void    schedNewDay();                      // midnight: every dose is fresh again
void    schedClockSet(uint16_t nowMin);     // clock set or restart: earlier doses count as handled
uint8_t schedCollectDue(uint16_t today, uint16_t nowMin, DueDose* out, uint8_t maxOut);

const uint8_t QUEUE_SIZE = 8;               // doses waiting to be announced
bool    queuePush(const DueDose& d);
bool    queuePop(DueDose& d);
uint8_t queueCount();
void    queueClear();
