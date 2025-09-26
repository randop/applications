#include <Arduino.h>

// Define the pin for the built-in LED
#define LED_PIN 13

// Setup function: Runs once at startup
void setup() {
  Serial.begin(115200);
  // Initialize the LED pin as an output
  pinMode(LED_PIN, OUTPUT);
  Serial.println("Running Blink project...");
}

// Loop function: Runs repeatedly
void loop() {
  digitalWrite(LED_PIN, HIGH);  // Turn LED on
  delay(3000);                  // Wait 1 second
  digitalWrite(LED_PIN, LOW);   // Turn LED off
  delay(3000);                  // Wait 1 second
}
