// selftest.cpp - checks every rule of the records and the scheduler using
// fake tags, fake dates and fake times. No hardware needed.
#include "selftest.h"
#include "records.h"
#include "scheduler.h"

static uint8_t passed, failed;

static void check(bool ok, const __FlashStringHelper* name) {
  Serial.print(ok ? F("[TEST] PASS  ") : F("[TEST] FAIL  "));
  Serial.println(name);
  if (ok) passed++; else failed++;
}

void runSelfTest() {
  static const uint8_t UID1[4]    = { 0x04, 0xA1, 0xB2, 0xC3 };
  static const uint8_t UID2[7]    = { 0x04, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
  static const uint8_t UNKNOWN[4] = { 0x11, 0x22, 0x33, 0x44 };
  static const uint8_t PREFIX[4]  = { 0x04, 0x11, 0x22, 0x33 };                    // start of UID2
  static const uint8_t PADDED[7]  = { 0x04, 0xA1, 0xB2, 0xC3, 0x00, 0x00, 0x00 };  // UID1 + zeros

  passed = failed = 0;
  Serial.println(F("[TEST] Starting. Records are reset to the 3 demo records."));
  recordsFormat();
  recordsSeedDemo();
  schedNewDay();
  queueClear();
  MedRecord r;

  // --- Records and checksum ---
  check(recordRead(0, r) == REC_OK && r.uidLen == 4, F("1  container 1 reads back with a good checksum"));
  check(recordRead(1, r) == REC_OK && r.uidLen == 7 && r.doseCount == 3,
        F("2  container 2 has a 7-byte UID and 3 doses"));
  check(recordRead(5, r) == REC_EMPTY, F("3  unused container 6 reads as empty"));

  // --- Lookup ---
  check(recordFind(UID1, 4, &r) == 0, F("4  4-byte UID finds container 1"));
  check(recordFind(UID2, 7, &r) == 1 && r.track == 2, F("5  7-byte UID finds container 2 (track 2)"));
  check(recordFind(UNKNOWN, 4, 0) == -1, F("6  unknown UID is not found"));
  check(recordFind(PREFIX, 4, 0) == -1, F("7  the first 4 bytes of a 7-byte UID don't match it"));
  check(recordFind(PADDED, 7, 0) == -1, F("8  a 4-byte UID padded to 7 bytes doesn't match"));

  // --- Duplicates ---
  MedRecord dup;
  recordRead(0, dup);                                   // same UID as container 1
  check(!recordSave(4, dup), F("9  the same UID can't be registered twice"));
  check(recordRead(4, r) == REC_EMPTY, F("10 ...and container 5 stays empty"));

  // --- Damaged data ---
  recordCorrupt(0);
  check(recordRead(0, r) == REC_BAD, F("11 a damaged record fails its checksum"));
  check(recordFind(UID1, 4, 0) == -1, F("12 a damaged record is never matched"));
  recordsSeedDemo();                                    // repair it

  // --- Scheduler, Fri 27 Nov 2026 ---
  const uint16_t TODAY = dateKey(26, 11, 27);
  DueDose due[QUEUE_SIZE];
  uint8_t n;

  schedClockSet(7 * 60 + 59);
  check(schedCollectDue(TODAY, 7 * 60 + 59, due, QUEUE_SIZE) == 0, F("13 nothing is due at 7:59 AM"));
  n = schedCollectDue(TODAY, 8 * 60, due, QUEUE_SIZE);
  check(n == 2 && due[0].rec == 0 && due[1].rec == 1 && due[1].dose == 0,
        F("14 at 8:00 AM containers 1 and 2 are both due, in order"));
  check(schedCollectDue(TODAY, 8 * 60, due, QUEUE_SIZE) == 0, F("15 checking 8:00 AM again fires nothing"));
  check(schedCollectDue(TODAY, 8 * 60 + 1, due, QUEUE_SIZE) == 0, F("16 nothing new at 8:01 AM"));
  n = schedCollectDue(TODAY, 13 * 60 + 30, due, QUEUE_SIZE);
  check(n == 1 && due[0].rec == 1 && due[0].dose == 1, F("17 container 2's 1:30 PM dose is due"));

  schedClockSet(14 * 60);                               // as if the Nano restarted at 2:00 PM
  check(schedCollectDue(TODAY, 14 * 60, due, QUEUE_SIZE) == 0,
        F("18 after a restart at 2:00 PM, earlier doses don't fire again"));
  check(schedCollectDue(TODAY, 19 * 60 + 5, due, QUEUE_SIZE) == 0,
        F("19 container 3's schedule ended on 1 Nov, so 7:05 PM fires nothing"));
  n = schedCollectDue(TODAY, 20 * 60, due, QUEUE_SIZE);
  check(n == 1 && due[0].rec == 1 && due[0].dose == 2, F("20 container 2's 8:00 PM dose is due"));

  schedClockSet(19 * 60);
  n = schedCollectDue(dateKey(26, 10, 31), 19 * 60 + 5, due, QUEUE_SIZE);
  check(n == 1 && due[0].rec == 2, F("21 on 31 Oct, container 3's dose still fires"));
  schedClockSet(19 * 60);
  n = schedCollectDue(dateKey(26, 11, 1), 19 * 60 + 5, due, QUEUE_SIZE);
  check(n == 1 && due[0].rec == 2, F("22 ...and on 1 Nov, its last valid day"));

  schedNewDay();
  n = schedCollectDue(dateKey(26, 11, 28), 8 * 60, due, QUEUE_SIZE);
  check(n == 2, F("23 after midnight, the 8:00 AM doses are due again"));

  schedNewDay();
  n = schedCollectDue(TODAY, 8 * 60, due, 1);
  check(n == 1 && due[0].rec == 0, F("24 with room for only 1, container 1 comes first"));
  n = schedCollectDue(TODAY, 8 * 60, due, QUEUE_SIZE);
  check(n == 1 && due[0].rec == 1, F("25 ...and container 2 isn't lost: it comes on the next check"));

  // --- Waiting queue ---
  queueClear();
  bool allIn = true;
  for (uint8_t k = 0; k < QUEUE_SIZE; k++) {
    DueDose dd = { k, 0 };
    allIn = queuePush(dd) && allIn;
  }
  DueDose extra = { 9, 0 };
  check(allIn && !queuePush(extra), F("26 the queue holds 8 doses and refuses a 9th"));
  DueDose a, b;
  queuePop(a);
  queuePop(b);
  check(a.rec == 0 && b.rec == 1 && queueCount() == QUEUE_SIZE - 2, F("27 the queue gives doses back in order"));
  queueClear();

  // --- Dates ---
  uint8_t y = 26, m = 11, d = 30;
  nextDay(y, m, d);
  check(y == 26 && m == 12 && d == 1, F("28 30 Nov 2026 -> 1 Dec 2026"));
  y = 26; m = 12; d = 31;
  nextDay(y, m, d);
  check(y == 27 && m == 1 && d == 1, F("29 31 Dec 2026 -> 1 Jan 2027"));
  y = 28; m = 2; d = 28;
  nextDay(y, m, d);
  check(m == 2 && d == 29, F("30 2028 is a leap year: 28 Feb -> 29 Feb"));
  check(dateKey(26, 12, 31) < dateKey(27, 1, 1), F("31 dates compare in the right order"));
  check(!dateValid(27, 2, 29) && dateValid(28, 2, 29), F("32 29 Feb is rejected in 2027, accepted in 2028"));

  // --- Clean up ---
  recordsFormat();
  recordsSeedDemo();
  schedNewDay();
  queueClear();
  Serial.print(F("[TEST] Done: "));
  Serial.print(passed);
  Serial.print(F(" passed, "));
  Serial.print(failed);
  Serial.println(failed ? F(" FAILED") : F(" failed. ALL PASS"));
}
