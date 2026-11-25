// smart_med.ino - main sketch for the Smart Medication Device (Arduino Nano).
//
// Day 2 (Thu 26 Nov): build the skeleton here, that is the state machine plus
// Serial Monitor "fake input" commands (type a tag UID or a time), so the
// logic can be tested before the parts arrive.

#include "pins.h"  // pin numbers for every module, kept in one place

void setup() {
  Serial.begin(9600);  // Serial Monitor speed: set the monitor to 9600 baud too
  // F("...") keeps the text in flash memory instead of copying it into the
  // Nano's 2 KB of RAM. Use it for every fixed message you print.
  Serial.println(F("Smart Medication Device: starting"));
}

void loop() {
  // The state machine goes here.
}
