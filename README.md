# Smart-Medication-device

A device which would help elderly people and blind people with medication intake.

It is an offline assistive prototype built on an Arduino Nano. Tap a medicine container's RFID tag on the reader and the device says which container it is. At dose times it reminds the user with a voice message, a beep and an LED. Four buttons let the user repeat a message, acknowledge a reminder, or hear the time and the next reminder.

> Physics laboratory project, CSD-C. This is an assistive prototype, not a certified medical device. Always check the medicine label.

## Team

| Name | Role |
| --- | --- |
| Vigneshwar | Software Lead |
| Vidwath | Hardware Lead |
| Kaushik | Audio and Test Lead |
| Sharan | Documentation and Procurement Lead |

## Folder layout

| Folder | What goes there |
| --- | --- |
| `firmware/smart_med/` | The main device sketch. `pins.h` holds the pin map shown below. |
| `tests/` | Small test sketches, one folder each. `blink/` is the first. |
| `voice/` | The 88-line voice script and the program that turns it into MP3 files. |
| `docs/` | The project plan (v2.0), and later the diagrams, photos and report. |

Every sketch lives in a folder with the same name as its `.ino` file, for example `smart_med/smart_med.ino`. The Arduino IDE expects this.

## Hardware and pin map

Parts: Arduino Nano V3 (ATmega328P, CH340), RC522 RFID reader with 13.56 MHz tags, DS3231 RTC (ZS-042 board) with a CR2032, DFPlayer Mini with an 8 Ω speaker and a FAT32 microSD card, active buzzer module, green and red LEDs, and 4 push buttons.

| Nano pin | Connects to | Notes |
| --- | --- | --- |
| D2 | DFPlayer TX | Direct |
| D3 | DFPlayer RX | Through a voltage divider |
| D4 | DFPlayer BUSY | LOW while a track is playing |
| D5 | Button 1: Repeat | Other leg to GND; `INPUT_PULLUP` |
| D6 | Button 2: Acknowledge | Other leg to GND; `INPUT_PULLUP` |
| D7 | Button 3: Next due / time | Other leg to GND; `INPUT_PULLUP` |
| D8 | Button 4: Caregiver (hold 5 s) | Other leg to GND; `INPUT_PULLUP` |
| D9 | RC522 RST | Through a voltage divider |
| D10 | RC522 SDA (SS) | Through a voltage divider |
| D11 | RC522 MOSI | Through a voltage divider |
| D12 | RC522 MISO | Direct |
| D13 | RC522 SCK | Through a voltage divider |
| A0 | Buzzer module I/O | Check whether it beeps on HIGH or LOW before coding |
| A1 | Green LED (status) | 220 Ω resistor |
| A2 | Red LED (alert) | 220 Ω resistor |
| A4 / A5 | DS3231 SDA / SCL | I2C address 0x68 (0x57 is the board's EEPROM) |

The RC522 runs from the Nano's 3V3 pin, never from 5 V. D0 and D1 stay free for uploading and the Serial Monitor. The full wiring, power notes, risks and the day-by-day timeline are in `docs/Smart_Medication_Device_Project_Plan_v2.pdf`.

## Software setup

1. Install Arduino IDE 2 from https://www.arduino.cc/en/software.
2. In the Library Manager, install **MFRC522** (by GithubCommunity), **RTClib** (by Adafruit; click Install All to add Adafruit BusIO) and **DFRobotDFPlayerMini** (by DFRobot).
3. Set Tools → Board to **Arduino Nano** and Tools → Processor to **ATmega328P (Old Bootloader)**.
4. Open `tests/blink/blink.ino` and click Verify. Upload it once a Nano is plugged in.

## Voice files

The device speaks by playing numbered MP3 files from the microSD card. `voice/make_voice_files.py` makes all 88 of them from `voice/voice_script.csv` with a free AI voice (internet needed, no account):

```
cd voice
pip install edge-tts
python make_voice_files.py
```

Copy the `mp3` folder it creates to the root of the FAT32 microSD card, so the card has `\mp3\0001.mp3` and so on. Once the voice is final, commit the `mp3` folder too, so anyone can load a card without running the script.

## Working rules

- Pull before you start, and push at least once a day, even unfinished work.
- Write a commit message that says what changed, for example "Add RFID tag lookup".
- Change `pins.h` only together with the wiring, and tell the hardware lead.
