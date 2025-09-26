#include <Arduino.h>
#include <Ds1302.h>

// Define the pin for the built-in LED
#define LED_PIN 13

/*
 * Arduino MH-Real Time Clock Module - 2 (DS1302)
 * 
 * Hardware Connections (for Arduino Uno):
 * - VCC to 5V
 * - GND to GND
 * - RST (CE) to Digital Pin 5
 * - DAT (IO) to Digital Pin 6
 * - CLK (SCLK) to Digital Pin 7
 */
#define PIN_RST 5
#define PIN_CLK 7
#define PIN_DAT 6
// DS1302 RTC instance
Ds1302 rtc(PIN_RST, PIN_CLK, PIN_DAT);

const static char* WeekDays[] =
{
    "Monday",
    "Tuesday",
    "Wednesday",
    "Thursday",
    "Friday",
    "Saturday",
    "Sunday"
};

// Setup function: Runs once at startup
void setup() {
  Serial.begin(115200);
  // Initialize the LED pin as an output
  pinMode(LED_PIN, OUTPUT);
  Serial.println("Running arduino project v0.1.1 ...");

// initialize the RTC
    rtc.init();

    // test if clock is halted and set a date-time (see example 2) to start it
    if (rtc.isHalted())
    {
        Serial.println("RTC is halted. Setting time...");

        Ds1302::DateTime dt = {
            .year = 25,
            .month = Ds1302::MONTH_SEP,
            .day = 27,
            .hour = 2,
            .minute = 44,
            .second = 30,
            .dow = Ds1302::DOW_SAT
        };

        rtc.setDateTime(&dt);
    }
}

// Loop function: Runs repeatedly
void loop() {
  digitalWrite(LED_PIN, HIGH);  // Turn LED on
  delay(1500);                  // Wait 1 second
  digitalWrite(LED_PIN, LOW);   // Turn LED off
  delay(1500);                  // Wait 1 second

  Ds1302::DateTime now;
  rtc.getDateTime(&now);

  static uint8_t last_second = 0;
    if (last_second != now.second)
    {
        last_second = now.second;

        Serial.print("20");
        Serial.print(now.year);    // 00-99
        Serial.print('-');
        if (now.month < 10) Serial.print('0');
        Serial.print(now.month);   // 01-12
        Serial.print('-');
        if (now.day < 10) Serial.print('0');
        Serial.print(now.day);     // 01-31
        Serial.print(' ');
        Serial.print(WeekDays[now.dow - 1]); // 1-7
        Serial.print(' ');
        if (now.hour < 10) Serial.print('0');
        Serial.print(now.hour);    // 00-23
        Serial.print(':');
        if (now.minute < 10) Serial.print('0');
        Serial.print(now.minute);  // 00-59
        Serial.print(':');
        if (now.second < 10) Serial.print('0');
        Serial.print(now.second);  // 00-59
        Serial.println();
    }
}
