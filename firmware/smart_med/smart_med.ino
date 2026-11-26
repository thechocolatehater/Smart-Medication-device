// smart_med.ino - Day 2 code skeleton
// State machine driven by fake inputs typed into the Serial Monitor.
// No real hardware is needed yet: speech, buzzer and log are printed as
// [SAY], [BUZZ] and [LOG] lines. Serial Monitor: 9600 baud, "Newline".

#include "pins.h"

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

// ---------------- Fake medicine records (EEPROM comes on Day 3) ----------------
const uint8_t MAX_UID = 7;
struct Record {
  uint8_t  slot;          // container number 1-10
  uint8_t  uidLen;        // 4 or 7
  uint8_t  uid[MAX_UID];
  uint16_t doseMin;       // one dose time, minutes after midnight
};

Record records[] = {
  { 1, 4, { 0x04, 0xA1, 0xB2, 0xC3 },                   8 * 60 },       // 8:00 AM
  { 2, 7, { 0x04, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 }, 13 * 60 + 30 },  // 1:30 PM
  { 3, 4, { 0xDE, 0xAD, 0xBE, 0xEF },                   19 * 60 + 5 },   // 7:05 PM
};
const char* const NAMES[] = { "Paracetamol", "Metformin", "Amlodipine" };
const uint8_t RECORD_COUNT = sizeof(records) / sizeof(records[0]);

// ---------------- Scan memory ----------------
uint8_t lastUid[MAX_UID];
uint8_t lastUidLen = 0;
unsigned long lastScanMs = 0;
int8_t lastResultIdx = -2;        // -1 = unknown tag, -2 = nothing scanned yet

// ---------------- Fake clock ----------------
bool timeSet = false;
uint16_t clockMin = 0;            // minutes after midnight
unsigned long lastMinuteMs = 0;
uint16_t lastCheckedMin = 0xFFFF;

// ---------------- Reminder ----------------
int8_t remindIdx = -1;            // which record is being reminded
uint8_t remindCount = 0;          // announcements so far
unsigned long lastRemindMs = 0;
int8_t pendingIdx = -1;           // dose that fell due while we were busy

// ---------------- Serial line buffer ----------------
char lineBuf[32];
uint8_t lineLen = 0;

// =====================================================================
// Small helpers
// =====================================================================
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

void printTime(uint16_t m) {      // prints 7:05 PM style
  uint8_t h = m / 60;
  uint8_t mm = m % 60;
  uint8_t h12 = h % 12;
  if (h12 == 0) h12 = 12;
  Serial.print(h12);
  Serial.print(':');
  if (mm < 10) Serial.print('0');
  Serial.print(mm);
  Serial.print(h < 12 ? F(" AM") : F(" PM"));
}

void logWrite(int8_t idx, bool acked) {   // EEPROM log comes later; print for now
  Serial.print(F("[LOG] Container "));
  Serial.print(records[idx].slot);
  Serial.print(acked ? F(": acknowledged") : F(": MISSED"));
  if (timeSet) { Serial.print(F(" at ")); printTime(clockMin); }
  Serial.println();
}

// =====================================================================
// Scanning
// =====================================================================
int8_t findRecord(const uint8_t* uid, uint8_t len) {
  for (uint8_t i = 0; i < RECORD_COUNT; i++) {
    if (records[i].uidLen == len && memcmp(records[i].uid, uid, len) == 0) return i;
  }
  return -1;
}

void sayScanResult() {
  if (lastResultIdx >= 0) {
    digitalWrite(PIN_LED_GREEN, HIGH);
    Serial.print(F("[SAY] Container "));
    Serial.print(records[lastResultIdx].slot);
    Serial.print(F(": "));
    Serial.print(NAMES[lastResultIdx]);
    Serial.println(F(". Please check the label."));
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

  lastResultIdx = findRecord(uid, len);
  sayScanResult();
  changeState(SCAN_RESULT);
}

// =====================================================================
// Clock and reminders
// =====================================================================
void announceReminder() {
  Serial.println(F("[BUZZ] reminder beep"));
  Serial.print(F("[SAY] Reminder... Container "));
  Serial.print(records[remindIdx].slot);
  Serial.print(F(".  (announcement "));
  Serial.print(remindCount);
  Serial.print(F(" of "));
  Serial.print(REMIND_MAX);
  Serial.println(F(")"));
}

void startReminder(int8_t idx) {
  remindIdx = idx;
  remindCount = 1;
  lastRemindMs = millis();
  changeState(REMINDING);
  announceReminder();
}

void checkDoses() {
  if (clockMin == lastCheckedMin) return;   // each minute is checked once
  lastCheckedMin = clockMin;
  for (uint8_t i = 0; i < RECORD_COUNT; i++) {
    if (records[i].doseMin != clockMin) continue;
    if (state == IDLE || state == SCAN_RESULT) {
      startReminder(i);
    } else {
      pendingIdx = i;                        // simple one-slot queue for now
      Serial.println(F("[INFO] Dose due while busy: queued"));
    }
  }
}

void clockUpdate() {
  if (!timeSet) return;
  if (millis() - lastMinuteMs >= 60000UL) {
    lastMinuteMs += 60000UL;
    clockMin = (clockMin + 1) % 1440;
    checkDoses();
  }
}

void sayTimeAndNext() {
  if (!timeSet) {
    Serial.println(F("[SAY] Time not set."));
    return;
  }
  Serial.print(F("[SAY] The time is "));
  printTime(clockMin);
  Serial.println('.');

  int8_t best = -1;
  uint16_t bestDiff = 0;
  for (uint8_t i = 0; i < RECORD_COUNT; i++) {
    uint16_t diff = (records[i].doseMin + 1440 - clockMin) % 1440;
    if (diff == 0) diff = 1440;              // due now counts as tomorrow's
    if (best < 0 || diff < bestDiff) { best = i; bestDiff = diff; }
  }
  if (best >= 0) {
    Serial.print(F("[SAY] Next: container "));
    Serial.print(records[best].slot);
    Serial.print(F(" at "));
    printTime(records[best].doseMin);
    Serial.println('.');
  }
}

// =====================================================================
// Serial commands (fake tag, fake time, fake buttons)
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
  Serial.println(F("  tag 04a1b2c3     fake scan (4 or 7 bytes in hex)"));
  Serial.println(F("  time 19:05       set the fake clock (24-hour)"));
  Serial.println(F("  ack / repeat / next   fake buttons D6 / D5 / D7"));
  Serial.println(F("  care / exit      enter / leave caregiver mode (D8 hold)"));
  Serial.println(F("  state / help"));
  Serial.println(F("Test tags: 04a1b2c3 (1, 8:00 AM)  04112233445566 (2, 1:30 PM)  deadbeef (3, 7:05 PM)"));
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
    lastCheckedMin = 0xFFFF;                         // check this minute again
    Serial.print(F("[INFO] Clock set to "));
    printTime(clockMin);
    Serial.println();
    checkDoses();

  } else if (strcmp(cmd, "ack") == 0) {
    if (state == REMINDING) {
      logWrite(remindIdx, true);
      Serial.println(F("[SAY] Noted."));
      remindIdx = -1;
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

  } else if (strcmp(cmd, "state") == 0) {
    Serial.print(F("[INFO] State: "));
    Serial.print(STATE_NAMES[state]);
    Serial.print(F("   Time: "));
    if (timeSet) printTime(clockMin); else Serial.print(F("not set"));
    Serial.println();

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
  Serial.println(F("Smart Medication Device - skeleton v0.2 (fake inputs)"));
  printHelp();
}

void loop() {
  serialPoll();
  clockUpdate();

  switch (state) {
    case IDLE:
      if (pendingIdx >= 0) {                  // a dose came due while we were busy
        int8_t i = pendingIdx;
        pendingIdx = -1;
        startReminder(i);
      }
      break;

    case SCAN_RESULT:
      if (millis() - stateStartMs >= SCAN_SHOW_MS) changeState(IDLE);
      break;

    case REMINDING:
      if (millis() - lastRemindMs >= REMIND_REPEAT_MS) {
        if (remindCount >= REMIND_MAX) {
          logWrite(remindIdx, false);
          remindIdx = -1;
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
