// records.cpp - reading, saving and checking the medicine records in EEPROM.
#include "records.h"
#include <EEPROM.h>

// EEPROM map (the Nano has 1024 bytes):
//   0-2     header: 'S', 'M', layout version
//   16-255  10 records x 24 bytes
//   256+    free (the acknowledgment log comes later)
const int     HEADER_ADDR    = 0;
const int     RECORDS_ADDR   = 16;
const uint8_t LAYOUT_VERSION = 1;

static int recordAddr(uint8_t i) { return RECORDS_ADDR + i * sizeof(MedRecord); }

// CRC-8 (polynomial 0x07). Changing any single byte changes the result,
// so a damaged record no longer matches its stored checksum.
uint8_t crc8(const uint8_t* data, uint8_t len) {
  uint8_t crc = 0;
  while (len--) {
    crc ^= *data++;
    for (uint8_t b = 0; b < 8; b++) crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
  }
  return crc;
}

static uint8_t recordCrc(const MedRecord& r) {
  return crc8((const uint8_t*)&r, sizeof(MedRecord) - 1);   // every byte except crc itself
}

// Values that can't be right even if the checksum matches.
static bool recordLooksValid(const MedRecord& r, uint8_t i) {
  if (r.slot != i + 1) return false;
  if (r.uidLen != 4 && r.uidLen != 7) return false;
  if (r.doseCount < 1 || r.doseCount > MAX_DOSES) return false;
  for (uint8_t d = 0; d < r.doseCount; d++) {
    if (r.doseMin[d] >= 1440) return false;
  }
  return true;
}

RecStatus recordRead(uint8_t i, MedRecord& r) {
  if (i >= MAX_RECORDS) return REC_EMPTY;
  EEPROM.get(recordAddr(i), r);
  if (!(r.flags & FLAG_USED)) return REC_EMPTY;
  if (r.crc != recordCrc(r) || !recordLooksValid(r, i)) return REC_BAD;
  return REC_OK;
}

bool recordSave(uint8_t i, MedRecord& r) {
  if (i >= MAX_RECORDS) return false;
  MedRecord other;
  for (uint8_t j = 0; j < MAX_RECORDS; j++) {         // the same tag can't be on two containers
    if (j == i) continue;
    if (recordRead(j, other) == REC_OK && other.uidLen == r.uidLen &&
        memcmp(other.uid, r.uid, r.uidLen) == 0) return false;
  }
  r.flags |= FLAG_USED;
  r.slot = i + 1;
  for (uint8_t k = r.uidLen; k < MAX_UID; k++) r.uid[k] = 0;
  for (uint8_t d = r.doseCount; d < MAX_DOSES; d++) r.doseMin[d] = 0;
  if (!recordLooksValid(r, i)) return false;
  r.crc = recordCrc(r);
  EEPROM.put(recordAddr(i), r);   // put() only rewrites bytes that changed, which saves EEPROM wear
  return true;
}

void recordErase(uint8_t i) {
  if (i < MAX_RECORDS) EEPROM.update(recordAddr(i), 0);   // flags = 0: slot unused
}

int8_t recordFind(const uint8_t* uid, uint8_t len, MedRecord* out) {
  MedRecord r;
  for (uint8_t i = 0; i < MAX_RECORDS; i++) {
    if (recordRead(i, r) != REC_OK) continue;          // empty or damaged records never match
    if (r.uidLen == len && memcmp(r.uid, uid, len) == 0) {
      if (out) *out = r;
      return i;
    }
  }
  return -1;
}

void recordsFormat() {
  for (uint8_t i = 0; i < MAX_RECORDS; i++) recordErase(i);
  EEPROM.update(HEADER_ADDR, 'S');
  EEPROM.update(HEADER_ADDR + 1, 'M');
  EEPROM.update(HEADER_ADDR + 2, LAYOUT_VERSION);
}

static void seedOne(uint8_t i, const uint8_t* uid, uint8_t len, uint8_t doses, const uint16_t* mins,
                    uint8_t vy, uint8_t vm, uint8_t vd) {
  MedRecord r;
  memset(&r, 0, sizeof(r));
  r.uidLen = len;
  memcpy(r.uid, uid, len);
  r.track = i + 1;
  r.doseCount = doses;
  memcpy(r.doseMin, mins, doses * sizeof(uint16_t));
  r.validY = vy;
  r.validM = vm;
  r.validD = vd;
  recordSave(i, r);
}

void recordsSeedDemo() {
  static const uint8_t  U1[] = { 0x04, 0xA1, 0xB2, 0xC3 };
  static const uint8_t  U2[] = { 0x04, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
  static const uint8_t  U3[] = { 0xDE, 0xAD, 0xBE, 0xEF };
  static const uint16_t T1[] = { 8 * 60 };                            // 8:00 AM
  static const uint16_t T2[] = { 8 * 60, 13 * 60 + 30, 20 * 60 };     // 8:00 AM, 1:30 PM, 8:00 PM
  static const uint16_t T3[] = { 19 * 60 + 5 };                       // 7:05 PM
  seedOne(0, U1, 4, 1, T1, 26, 12, 31);
  seedOne(1, U2, 7, 3, T2, 26, 12, 31);
  seedOne(2, U3, 4, 1, T3, 26, 11, 1);   // ended on 1 Nov 2026: tests the "schedule expired" rule
}

void recordsBegin() {
  if (EEPROM.read(HEADER_ADDR) != 'S' || EEPROM.read(HEADER_ADDR + 1) != 'M' ||
      EEPROM.read(HEADER_ADDR + 2) != LAYOUT_VERSION) {
    Serial.println(F("[INFO] EEPROM not set up yet: formatting it and loading 3 demo records"));
    recordsFormat();
    recordsSeedDemo();
  }
  MedRecord r;
  uint8_t used = 0;
  for (uint8_t i = 0; i < MAX_RECORDS; i++) {
    RecStatus st = recordRead(i, r);
    if (st == REC_OK) used++;
    if (st == REC_BAD) {
      Serial.print(F("[WARN] Container "));
      Serial.print(i + 1);
      Serial.println(F(": record damaged (checksum failed), ignored"));
    }
  }
  Serial.print(F("[INFO] "));
  Serial.print(used);
  Serial.println(F(" container(s) loaded from EEPROM"));
}

void recordsList() {
  MedRecord r;
  uint8_t used = 0;
  for (uint8_t i = 0; i < MAX_RECORDS; i++) {
    RecStatus st = recordRead(i, r);
    if (st == REC_EMPTY) continue;
    Serial.print(F("  Container "));
    Serial.print(i + 1);
    if (st == REC_BAD) {
      Serial.println(F(": DAMAGED (checksum failed), ignored"));
      continue;
    }
    used++;
    Serial.print(F("  UID "));
    printUid(r.uid, r.uidLen);
    Serial.print(F("  track "));
    Serial.print(r.track);
    Serial.print(F("  doses "));
    for (uint8_t d = 0; d < r.doseCount; d++) {
      if (d) Serial.print(F(", "));
      printTime(r.doseMin[d]);
    }
    Serial.print(F("  valid until "));
    printDate(r.validY, r.validM, r.validD);
    Serial.println();
  }
  Serial.print(F("[INFO] "));
  Serial.print(used);
  Serial.println(F(" of 10 containers registered"));
}

void recordCorrupt(uint8_t i) {
  if (i >= MAX_RECORDS) return;
  int a = recordAddr(i) + 12;                   // low byte of the first dose time
  EEPROM.update(a, EEPROM.read(a) ^ 0x01);
}

void printTime(uint16_t minutes) {
  uint8_t h = minutes / 60;
  uint8_t mm = minutes % 60;
  uint8_t h12 = h % 12;
  if (h12 == 0) h12 = 12;
  Serial.print(h12);
  Serial.print(':');
  if (mm < 10) Serial.print('0');
  Serial.print(mm);
  Serial.print(h < 12 ? F(" AM") : F(" PM"));
}

void printDate(uint8_t y, uint8_t m, uint8_t d) {
  Serial.print(2000 + y);
  Serial.print('-');
  if (m < 10) Serial.print('0');
  Serial.print(m);
  Serial.print('-');
  if (d < 10) Serial.print('0');
  Serial.print(d);
}

void printUid(const uint8_t* uid, uint8_t len) {
  for (uint8_t i = 0; i < len; i++) {
    if (i) Serial.print(' ');
    if (uid[i] < 0x10) Serial.print('0');
    Serial.print(uid[i], HEX);
  }
}
