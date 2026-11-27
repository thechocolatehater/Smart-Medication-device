// smart_med.ino - Day 3: medicine records in EEPROM, UID lookup and the
// dose scheduler, still driven by fake inputs typed into the Serial Monitor.
// Speech, buzzer and log are printed as [SAY], [BUZZ] and [LOG] lines.
// Serial Monitor: 9600 baud, "Newline".

#include "pins.h"
#include "records.h"
#include "scheduler.h"
#include "selftest.h"

// ---------------- States ----------------
enum State : uint8_t { IDLE, SCAN_RESULT, REMINDING, CAREGIVER };
const char* const STATE_NAMES[] = { "IDLE", "SCAN_RESULT", "REMINDING", "CAREGIVER" };

State state = IDLE;
unsigned long stateStartMs = 0;   // when we entered the current state

// ---------------- Timings (all millis based, no delay()) ----------------
const unsigned long SCAN_SHOW_MS     = 3000UL;    // stay in SCAN_RESULT for 3 s
const unsigned long DUP_GUARD_MS     = 2000UL;    // same tag within 2 s is ignored
const unsigned long REMIND_REPEAT_MS = 300000UL;  // repeat a reminder every 5 min (use 20000UL while testing)
const uint8_t       REMIND_MAX       = 3;         // after 3 announcements with no Ack -> missed
const unsigned long CARE_TIMEOUT_MS  = 60000UL;   // leave caregiver mode after 60 s of no commands

// ---------------- Scan memory ----------------
uint8_t lastUid[MAX_UID];
uint8_t lastUidLen = 0;
unsigned long lastScanMs = 0;
int8_t lastResultIdx = -2;        // -1 = unknown tag, -2 = nothing scanned yet
MedRecord lastRec;                // copy of the record that was scanned

// ---------------- Fake clock and calendar (the DS3231 replaces these on Day 8) ----------------
bool timeSet = false;
bool dateSet = false;
uint16_t clockMin = 0;            // minutes after midnight
uint8_t dateY = 0, dateM = 0, dateD = 0;   // dateY = year - 2000
unsigned long lastMinuteMs = 0;
uint16_t lastCheckedMin = 0xFFFF;

// ---------------- Reminder ----------------
DueDose cur;                      // the dose being announced
uint8_t remindCount = 0;          // announcements so far
unsigned long lastRemindMs = 0;

// ---------------- Serial line buffer ----------------
char lineBuf[32];
uint8_t lineLen = 0;

// =====================================================================
// Small helpers
// =====================================================================
bool clockReady() { return timeSet && dateSet; }      // reminders need both
uint16_t today() { return dateKey(dateY, dateM, dateD); }

void changeState(State next) {
  Serial.print(F("[STATE] "));
  Serial.print(STATE_NAMES[state]);
  Serial.print(F(" -> "));
  Serial.println(STATE_NAMES[next]);
  state = next;
  stateStartMs = millis();
  if (next == IDLE) {             // LEDs off whenever we are back to waiting
    digitalWrite(PIN_LED_GREEN, LOW);
    digitalWrite(PIN_LED_RED, LOW);
  }
}

void logWrite(const DueDose& d, bool acked) {   // EEPROM log comes later; print for now
  MedRecord r;
  Serial.print(F("[LOG] Container "));
  Serial.print(d.rec + 1);
  if (recordRead(d.rec, r) == REC_OK) {
    Serial.print(F(", "));
    printTime(r.doseMin[d.dose]);
    Serial.print(F(" dose"));
  }
  Serial.print(acked ? F(": acknowledged (button pressed)") : F(": MISSED"));
  if (clockReady()) { Serial.print(F(" at ")); printTime(clockMin); }
  Serial.println();
}

// =====================================================================
// Scanning
// =====================================================================
void sayScanResult() {
  if (lastResultIdx >= 0) {
    bool expired = dateSet && recordExpired(lastRec, today());
    digitalWrite(expired ? PIN_LED_RED : PIN_LED_GREEN, HIGH);
    Serial.print(F("[SAY] (track "));
    Serial.print(lastRec.track);
    Serial.print(F(") Container "));
    Serial.print(lastRec.slot);
    Serial.println(F(". Please check the label."));
    if (expired) Serial.println(F("[SAY] Schedule expired. Ask a caregiver."));
  } else {
    digitalWrite(PIN_LED_RED, HIGH);
    Serial.println(F("[BUZZ] unknown-tag tone"));
    Serial.println(F("[SAY] Unregistered container."));
  }
}

void onTag(const uint8_t* uid, uint8_t len) {
  if (state == REMINDING || state == CAREGIVER) {
    Serial.println(F("[INFO] Tag ignored in this state"));
    return;
  }
  unsigned long now = millis();
  bool sameTag = (len == lastUidLen) && memcmp(uid, lastUid, len) == 0;
  if (sameTag && now - lastScanMs < DUP_GUARD_MS) {
    lastScanMs = now;             // tag still held: keep ignoring it
    Serial.println(F("[INFO] Same tag within 2 s: ignored"));
    return;
  }
  memcpy(lastUid, uid, len);
  lastUidLen = len;
  lastScanMs = now;

  lastResultIdx = recordFind(uid, len, &lastRec);   // looks through the EEPROM records
  sayScanResult();
  changeState(SCAN_RESULT);
}

// =====================================================================
// Clock and reminders
// =====================================================================
void announceReminder() {
  MedRecord r;
  Serial.println(F("[BUZZ] reminder beep"));
  Serial.print(F("[SAY] Reminder: please check your medicine schedule. Container "));
  Serial.print(cur.rec + 1);
  if (recordRead(cur.rec, r) == REC_OK) {
    Serial.print(F(", "));
    printTime(r.doseMin[cur.dose]);
    Serial.print(F(" dose"));
  }
  Serial.print(F(".  (announcement "));
  Serial.print(remindCount);
  Serial.print(F(" of "));
  Serial.print(REMIND_MAX);
  Serial.println(F(")"));
}

void startReminder(const DueDose& d) {
  cur = d;
  remindCount = 1;
  lastRemindMs = millis();
  changeState(REMINDING);
  announceReminder();
}

// Once a minute: ask the scheduler what is due and put it in the waiting queue.
void checkDoses() {
  if (!clockReady()) return;                 // "time not set": no reminders at all
  if (clockMin == lastCheckedMin) return;    // each minute is checked once
  lastCheckedMin = clockMin;
  DueDose due[QUEUE_SIZE];
  uint8_t n = schedCollectDue(today(), clockMin, due, QUEUE_SIZE - queueCount());
  for (uint8_t k = 0; k < n; k++) {
    queuePush(due[k]);
    Serial.print(F("[INFO] Due: container "));
    Serial.print(due[k].rec + 1);
    Serial.print(F("  ("));
    Serial.print(queueCount());
    Serial.println(F(" waiting)"));
  }
}

void clockUpdate() {
  if (!timeSet) return;
  if (millis() - lastMinuteMs >= 60000UL) {
    lastMinuteMs += 60000UL;
    clockMin++;
    if (clockMin >= 1440) {                  // midnight
      clockMin = 0;
      schedNewDay();
      if (dateSet) {
        nextDay(dateY, dateM, dateD);
        Serial.print(F("[INFO] New day: "));
        printDate(dateY, dateM, dateD);
        Serial.println();
      }
    }
  }
}

void sayTimeAndNext() {
  if (!clockReady()) {
    Serial.println(F("[SAY] Time not set. Ask a caregiver."));
    return;
  }
  Serial.print(F("[SAY] The time is "));
  printTime(clockMin);
  Serial.println('.');

  int8_t bestRec = -1;
  uint16_t bestMin = 0, bestDiff = 0;
  MedRecord r;
  for (uint8_t i = 0; i < MAX_RECORDS; i++) {
    if (recordRead(i, r) != REC_OK || recordExpired(r, today())) continue;
    for (uint8_t d = 0; d < r.doseCount; d++) {
      uint16_t diff = (r.doseMin[d] + 1440 - clockMin) % 1440;
      if (diff == 0) diff = 1440;            // due now counts as tomorrow's
      if (bestRec < 0 || diff < bestDiff) { bestRec = i; bestMin = r.doseMin[d]; bestDiff = diff; }
    }
  }
  if (bestRec >= 0) {
    Serial.print(F("[SAY] Next: container "));
    Serial.print(bestRec + 1);
    Serial.print(F(" at "));
    printTime(bestMin);
    Serial.println('.');
  } else {
    Serial.println(F("[SAY] No medicine is scheduled."));
  }
}

// =====================================================================
// Serial commands (fake tag, fake date and time, fake buttons)
// =====================================================================
int8_t hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// "04 a1 b2 c3" or "04a1b2c3" or "04:A1:..." -> bytes; returns 4 or 7, else 0
uint8_t parseUid(const char* s, uint8_t* out) {
  uint8_t n = 0;
  while (*s) {
    if (*s == ' ' || *s == ':') { s++; continue; }
    int8_t hi = hexVal(s[0]);
    if (hi < 0) return 0;
    int8_t lo = hexVal(s[1]);
    if (lo < 0) return 0;
    if (n >= MAX_UID) return 0;
    out[n++] = (hi << 4) | lo;
    s += 2;
  }
  return (n == 4 || n == 7) ? n : 0;
}

void printHelp() {
  Serial.println(F("Commands:"));
  Serial.println(F("  tag 04a1b2c3       fake scan (4 or 7 bytes in hex)"));
  Serial.println(F("  date 2026-11-27    set the fake date"));
  Serial.println(F("  time 19:05         set the fake clock (24-hour)"));
  Serial.println(F("  ack / repeat / next   fake buttons D6 / D5 / D7"));
  Serial.println(F("  care / exit        enter / leave caregiver mode (D8 hold)"));
  Serial.println(F("  list / state / help"));
  Serial.println(F("Caregiver mode only: seed (3 demo records), wipe, corrupt N, test"));
  Serial.println(F("Demo tags: 04a1b2c3 (container 1)  04112233445566 (container 2)  deadbeef (container 3, expired)"));
}

// After the records change, start the day's bookkeeping again.
void afterRecordsChanged() {
  queueClear();
  if (timeSet) schedClockSet(clockMin);
  lastCheckedMin = 0xFFFF;
  lastResultIdx = -2;
}

void handleCommand(char* cmd) {
  for (char* p = cmd; *p; p++) *p = tolower(*p);   // accept capitals too
  if (state == CAREGIVER) stateStartMs = millis();  // any command keeps caregiver mode alive

  if (strncmp(cmd, "tag ", 4) == 0) {
    uint8_t uid[MAX_UID];
    uint8_t len = parseUid(cmd + 4, uid);
    if (len == 0) { Serial.println(F("[ERR] UID must be 4 or 7 hex bytes")); return; }
    onTag(uid, len);

  } else if (strncmp(cmd, "time ", 5) == 0) {
    int h = atoi(cmd + 5);
    const char* colon = strchr(cmd + 5, ':');
    int m = colon ? atoi(colon + 1) : -1;
    if (h < 0 || h > 23 || m < 0 || m > 59) { Serial.println(F("[ERR] Use time HH:MM, e.g. time 19:05")); return; }
    clockMin = h * 60 + m;
    timeSet = true;
    lastMinuteMs = millis();
    schedClockSet(clockMin);                         // earlier doses today don't fire
    lastCheckedMin = 0xFFFF;                         // check this minute again
    Serial.print(F("[INFO] Clock set to "));
    printTime(clockMin);
    Serial.println();
    if (!dateSet) Serial.println(F("[INFO] Set the date too: date YYYY-MM-DD"));

  } else if (strncmp(cmd, "date ", 5) == 0) {
    int y = atoi(cmd + 5);
    const char* d1 = strchr(cmd + 5, '-');
    const char* d2 = d1 ? strchr(d1 + 1, '-') : 0;
    int m = d1 ? atoi(d1 + 1) : 0;
    int d = d2 ? atoi(d2 + 1) : 0;
    if (y < 2000 || y > 2099 || !dateValid(y - 2000, m, d)) {
      Serial.println(F("[ERR] Use date YYYY-MM-DD, e.g. date 2026-11-27"));
      return;
    }
    dateY = y - 2000; dateM = m; dateD = d;
    dateSet = true;
    if (timeSet) schedClockSet(clockMin);
    lastCheckedMin = 0xFFFF;
    Serial.print(F("[INFO] Date set to "));
    printDate(dateY, dateM, dateD);
    Serial.println();
    if (!timeSet) Serial.println(F("[INFO] Set the time too: time HH:MM"));

  } else if (strcmp(cmd, "ack") == 0) {
    if (state == REMINDING) {
      logWrite(cur, true);
      Serial.println(F("[SAY] Reminder acknowledged."));
      changeState(IDLE);
    } else {
      Serial.println(F("[INFO] Nothing to acknowledge"));
    }

  } else if (strcmp(cmd, "repeat") == 0) {
    if (state == REMINDING) {
      announceReminder();                            // a repeat does not count towards missed
    } else if (state == SCAN_RESULT || (state == IDLE && lastResultIdx != -2)) {
      sayScanResult();
      changeState(SCAN_RESULT);
    } else {
      Serial.println(F("[INFO] Nothing to repeat"));
    }

  } else if (strcmp(cmd, "next") == 0) {
    sayTimeAndNext();

  } else if (strcmp(cmd, "care") == 0) {
    if (state == IDLE) {
      Serial.println(F("[SAY] Caregiver mode."));
      changeState(CAREGIVER);
    } else {
      Serial.println(F("[INFO] Caregiver mode only from IDLE"));
    }

  } else if (strcmp(cmd, "exit") == 0) {
    if (state == CAREGIVER) changeState(IDLE);

  } else if (strcmp(cmd, "list") == 0) {
    recordsList();

  } else if (strcmp(cmd, "seed") == 0 || strcmp(cmd, "wipe") == 0 ||
             strncmp(cmd, "corrupt ", 8) == 0 || strcmp(cmd, "test") == 0) {
    if (state != CAREGIVER) { Serial.println(F("[ERR] Caregiver mode only: type care first")); return; }
    if (cmd[0] == 's') {
      recordsFormat();
      recordsSeedDemo();
      Serial.println(F("[INFO] 3 demo records loaded"));
    } else if (cmd[0] == 'w') {
      recordsFormat();
      Serial.println(F("[INFO] All records erased"));
    } else if (cmd[0] == 'c') {
      int n = atoi(cmd + 8);
      if (n < 1 || n > MAX_RECORDS) { Serial.println(F("[ERR] Use corrupt 1 to corrupt 10")); return; }
      recordCorrupt(n - 1);
      Serial.print(F("[INFO] Container "));
      Serial.print(n);
      Serial.println(F(" damaged on purpose. Type seed to repair."));
    } else {
      runSelfTest();
    }
    afterRecordsChanged();

  } else if (strcmp(cmd, "state") == 0) {
    Serial.print(F("[INFO] State: "));
    Serial.print(STATE_NAMES[state]);
    Serial.print(F("   Date: "));
    if (dateSet) printDate(dateY, dateM, dateD); else Serial.print(F("not set"));
    Serial.print(F("   Time: "));
    if (timeSet) printTime(clockMin); else Serial.print(F("not set"));
    Serial.print(F("   Waiting: "));
    Serial.println(queueCount());

  } else if (strcmp(cmd, "help") == 0) {
    printHelp();

  } else {
    Serial.println(F("[ERR] Unknown command. Type help"));
  }
}

void serialPoll() {               // reads characters without blocking
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      lineBuf[lineLen] = '\0';
      if (lineLen > 0) handleCommand(lineBuf);
      lineLen = 0;
    } else if (lineLen < sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = c;
    }
  }
}

// =====================================================================
// setup() and loop()
// =====================================================================
void setup() {
  Serial.begin(9600);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_BTN_REPEAT, INPUT_PULLUP);
  pinMode(PIN_BTN_ACK, INPUT_PULLUP);
  pinMode(PIN_BTN_NEXT, INPUT_PULLUP);
  pinMode(PIN_BTN_CAREGIVER, INPUT_PULLUP);
  // Buzzer pin left alone until we know if the module beeps on HIGH or LOW (R9).

  stateStartMs = millis();
  Serial.println(F("Smart Medication Device - v0.3 (EEPROM records + scheduler, fake inputs)"));
  recordsBegin();
  printHelp();
  Serial.println(F("[SAY] Time not set. Ask a caregiver."));
}

void loop() {
  serialPoll();
  clockUpdate();
  checkDoses();

  switch (state) {
    case IDLE:
    case SCAN_RESULT: {
      DueDose d;
      if (queuePop(d)) { startReminder(d); break; }   // a waiting dose comes first
      if (state == SCAN_RESULT && millis() - stateStartMs >= SCAN_SHOW_MS) changeState(IDLE);
      break;
    }

    case REMINDING:
      if (millis() - lastRemindMs >= REMIND_REPEAT_MS) {
        if (remindCount >= REMIND_MAX) {
          logWrite(cur, false);
          changeState(IDLE);
        } else {
          remindCount++;
          lastRemindMs = millis();
          announceReminder();
        }
      }
      break;

    case CAREGIVER:
      if (millis() - stateStartMs >= CARE_TIMEOUT_MS) {
        Serial.println(F("[INFO] Caregiver mode timed out"));
        changeState(IDLE);
      }
      break;
  }
}
