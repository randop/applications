#include <Arduino.h>

// Blink.ino - Simple LED blink example for Arduino Uno (renamed to .cpp)

// Define the pin for the built-in LED
#define LED_PIN 13

// Setup function: Runs once at startup
void setup() {
  // Initialize the LED pin as an output
  pinMode(LED_PIN, OUTPUT);
}

// Loop function: Runs repeatedly
void loop() {
  digitalWrite(LED_PIN, HIGH);  // Turn LED on
  delay(3000);                  // Wait 1 second
  digitalWrite(LED_PIN, LOW);   // Turn LED off
  delay(3000);                  // Wait 1 second
}
