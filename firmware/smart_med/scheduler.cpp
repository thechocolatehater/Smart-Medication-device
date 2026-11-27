// scheduler.cpp - due check, once-a-day marks, expiry and the waiting queue.
#include "scheduler.h"

// One byte per container: bit d set = dose d has already been handled today.
static uint8_t handled[MAX_RECORDS];

uint16_t dateKey(uint8_t y, uint8_t m, uint8_t d) {
  return ((uint16_t)y << 9) | ((uint16_t)m << 5) | d;   // yyyyyyy mmmm ddddd
}

static uint8_t daysInMonth(uint8_t y, uint8_t m) {
  static const uint8_t DAYS[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  if (m == 2 && (y % 4) == 0) return 29;   // from 2000 to 2099, every 4th year is a leap year
  return DAYS[m - 1];
}

bool dateValid(uint8_t y, uint8_t m, uint8_t d) {
  return y <= 99 && m >= 1 && m <= 12 && d >= 1 && d <= daysInMonth(y, m);
}

void nextDay(uint8_t& y, uint8_t& m, uint8_t& d) {
  if (d < daysInMonth(y, m)) { d++; return; }
  d = 1;
  if (m < 12) { m++; return; }
  m = 1;
  y++;
}

bool recordExpired(const MedRecord& r, uint16_t today) {
  return today > dateKey(r.validY, r.validM, r.validD);   // the last valid day itself still counts
}

void schedNewDay() {
  memset(handled, 0, sizeof(handled));
}

void schedClockSet(uint16_t nowMin) {
  schedNewDay();
  MedRecord r;
  for (uint8_t i = 0; i < MAX_RECORDS; i++) {
    if (recordRead(i, r) != REC_OK) continue;
    for (uint8_t d = 0; d < r.doseCount; d++) {
      if (r.doseMin[d] < nowMin) handled[i] |= (1 << d);   // already past: don't fire it again
    }
  }
}

uint8_t schedCollectDue(uint16_t today, uint16_t nowMin, DueDose* out, uint8_t maxOut) {
  uint8_t n = 0;
  MedRecord r;
  for (uint8_t i = 0; i < MAX_RECORDS; i++) {
    if (recordRead(i, r) != REC_OK) continue;              // empty or damaged: skip
    bool expired = recordExpired(r, today);
    for (uint8_t d = 0; d < r.doseCount; d++) {
      uint8_t bit = 1 << d;
      if (handled[i] & bit) continue;                      // already done today
      if (r.doseMin[d] > nowMin) continue;                 // not due yet
      if (expired) {
        handled[i] |= bit;
        Serial.print(F("[INFO] Container "));
        Serial.print(i + 1);
        Serial.println(F(": schedule expired, reminder skipped"));
        continue;
      }
      if (n >= maxOut) continue;                           // no room: stays due for the next check
      handled[i] |= bit;
      DueDose dd = { i, d };
      out[n++] = dd;
    }
  }
  return n;
}

// Waiting queue: a small ring buffer, first in, first out.
static DueDose q[QUEUE_SIZE];
static uint8_t qHead = 0, qCount = 0;

bool queuePush(const DueDose& d) {
  if (qCount >= QUEUE_SIZE) return false;
  q[(qHead + qCount) % QUEUE_SIZE] = d;
  qCount++;
  return true;
}

bool queuePop(DueDose& d) {
  if (qCount == 0) return false;
  d = q[qHead];
  qHead = (qHead + 1) % QUEUE_SIZE;
  qCount--;
  return true;
}

uint8_t queueCount() { return qCount; }

void queueClear() { qHead = 0; qCount = 0; }
