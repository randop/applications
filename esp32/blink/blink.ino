void setup() {
  pinMode(LED_BUILTIN, OUTPUT);  // LED_BUILTIN is usually GPIO 2 on ESP32
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);  // Turn LED on
  delay(1000);                      // Wait 1 second
  digitalWrite(LED_BUILTIN, LOW);   // Turn LED off
  delay(1000);                      // Wait 1 second
}
