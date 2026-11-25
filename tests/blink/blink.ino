// blink.ino - first check that the IDE, the board settings and the Nano work.
// Today: click Verify (compiles without a board).
// When the Nano arrives: plug it in, pick its COM port and click Upload.
// The Nano's "L" LED should then blink once a second.

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);  // LED_BUILTIN is D13 on the Nano
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);  // LED on
  delay(1000);                      // wait 1 second
  digitalWrite(LED_BUILTIN, LOW);   // LED off
  delay(1000);                      // wait 1 second
}
