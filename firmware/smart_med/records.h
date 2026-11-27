// records.h - medicine records stored in the Nano's EEPROM (Day 3).
// One record per container: which tag (UID) it has, which voice file
// announces it, up to 4 dose times a day, and the last day the schedule
// is valid. Each record carries a checksum, so damaged data is rejected
// instead of being trusted.
#pragma once
#include <Arduino.h>

const uint8_t MAX_RECORDS = 10;   // containers 1-10
const uint8_t MAX_UID     = 7;    // RFID UIDs are 4 or 7 bytes
const uint8_t MAX_DOSES   = 4;    // up to 4 dose times a day

// One container. Exactly 24 bytes, stored byte-for-byte in EEPROM.
struct MedRecord {
  uint8_t  flags;                  // bit 0 = slot in use
  uint8_t  slot;                   // container number 1-10
  uint8_t  uidLen;                 // 4 or 7
  uint8_t  uid[MAX_UID];           // unused bytes stay 0
  uint8_t  track;                  // voice file that names the container (0001-0010)
  uint8_t  doseCount;              // 1-4
  uint16_t doseMin[MAX_DOSES];     // dose times in minutes after midnight (8:00 AM = 480)
  uint8_t  validY, validM, validD; // last valid day; validY = year - 2000
  uint8_t  crc;                    // CRC-8 checksum of the 23 bytes above
};
static_assert(sizeof(MedRecord) == 24, "MedRecord must be 24 bytes");

enum RecStatus : uint8_t { REC_EMPTY, REC_OK, REC_BAD };
const uint8_t FLAG_USED = 0x01;

void      recordsBegin();                       // call once in setup()
RecStatus recordRead(uint8_t i, MedRecord& r);  // i = 0-9 means container i + 1
bool      recordSave(uint8_t i, MedRecord& r);  // false if the UID is used by another container
void      recordErase(uint8_t i);
int8_t    recordFind(const uint8_t* uid, uint8_t len, MedRecord* out);  // index 0-9, or -1
void      recordsFormat();                      // erase all 10 containers
void      recordsSeedDemo();                    // 3 demo containers for testing
void      recordsList();                        // print every container to Serial
void      recordCorrupt(uint8_t i);             // testing only: damage one byte on purpose
uint8_t   crc8(const uint8_t* data, uint8_t len);

// Printing helpers shared by every file.
void printTime(uint16_t minutes);               // 480 -> "8:00 AM"
void printDate(uint8_t y, uint8_t m, uint8_t d); // 26, 11, 27 -> "2026-11-27"
void printUid(const uint8_t* uid, uint8_t len); // "04 A1 B2 C3"
