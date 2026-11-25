// pins.h - Nano pin map for the Smart Medication Device.
// Copied from section 4.1 (pin-to-pin wiring table) of
// docs/Smart_Medication_Device_Project_Plan_v2.pdf.
// Change a pin here only when the wiring changes, and tell the hardware lead.
#pragma once
#include <Arduino.h>

// RC522 RFID reader (SPI). MOSI = D11, MISO = D12 and SCK = D13 are fixed by
// the Nano's SPI hardware, so only the two control pins need names.
const uint8_t PIN_RFID_RST = 9;
const uint8_t PIN_RFID_SS  = 10;

// DFPlayer Mini, driven through SoftwareSerial at 9600 baud.
const uint8_t PIN_DF_RX   = 2;  // Nano receives on D2 <- DFPlayer TX
const uint8_t PIN_DF_TX   = 3;  // Nano sends on D3 -> divider -> DFPlayer RX
const uint8_t PIN_DF_BUSY = 4;  // LOW while a track is playing

// Buttons. The other leg goes to GND, so set them as INPUT_PULLUP:
// the pin reads HIGH when released and LOW when pressed.
const uint8_t PIN_BTN_REPEAT    = 5;  // Button 1: repeat the last message
const uint8_t PIN_BTN_ACK       = 6;  // Button 2: acknowledge a reminder
const uint8_t PIN_BTN_NEXT      = 7;  // Button 3: next due / speak the time
const uint8_t PIN_BTN_CAREGIVER = 8;  // Button 4: hold 5 s for caregiver mode

// Outputs.
const uint8_t PIN_BUZZER    = A0;  // check whether it beeps on HIGH or LOW first (risk R9)
const uint8_t PIN_LED_GREEN = A1;  // status
const uint8_t PIN_LED_RED   = A2;  // alert

// The DS3231 RTC uses the I2C pins A4 (SDA) and A5 (SCL) through the Wire library.
const uint8_t I2C_ADDR_RTC = 0x68;
