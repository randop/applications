#define ARDUINO 101
#define SSD1306_NO_SPLASH

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Ds1302.h>
#include <Wire.h>

// Define the pin for the built-in LED
#define LED_PIN 13

#include "I2CScanner.h"

I2CScanner scanner;

const uint8_t num_addresses = 4;
const byte addresses[num_addresses] = {0x3D, 0x3C, 0x40, 0x41};
bool results[num_addresses] = {false, false, false, false};

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
Ds1302::DateTime now;

const static char *WeekDays[] = {"Monday", "Tuesday",  "Wednesday", "Thursday",
                                 "Friday", "Saturday", "Sunday"};

// OLED display width, in pixels
#define SCREEN_WIDTH 128
// OLED display height, in pixels
#define SCREEN_HEIGHT 64

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
// The pins for I2C are defined by the Wire-library.
// On an arduino UNO:       A4(SDA), A5(SCL)
// On an arduino MEGA 2560: 20(SDA), 21(SCL)
// On an arduino LEONARDO:   2(SDA),  3(SCL), ...
// #define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define OLED_RESET -1

// See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define LOGO_HEIGHT 16
#define LOGO_WIDTH 16
static const unsigned char PROGMEM logo_bmp[] = {
    0b00000000, 0b11000000, 0b00000001, 0b11000000, 0b00000001, 0b11000000,
    0b00000011, 0b11100000, 0b11110011, 0b11100000, 0b11111110, 0b11111000,
    0b01111110, 0b11111111, 0b00110011, 0b10011111, 0b00011111, 0b11111100,
    0b00001101, 0b01110000, 0b00011011, 0b10100000, 0b00111111, 0b11100000,
    0b00111111, 0b11110000, 0b01111100, 0b11110000, 0b01110000, 0b01110000,
    0b00000000, 0b00110000};

void testdrawbitmap(void) {
  display.clearDisplay();

  display.drawBitmap((display.width() - LOGO_WIDTH) / 2,
                     (display.height() - LOGO_HEIGHT) / 2, logo_bmp, LOGO_WIDTH,
                     LOGO_HEIGHT, 1);
  display.display();
  delay(1000);
}

// Setup function: Runs once at startup
void setup() {
  Serial.begin(115200);
  // Initialize the LED pin as an output
  pinMode(LED_PIN, OUTPUT);

  while (!Serial) {
  };

  scanner.Init();

  delay(5000);

  for (uint8_t index = 0; index < num_addresses; index++) {
    results[index] = scanner.Check(addresses[index]);
  }

  for (uint8_t index = 0; index < num_addresses; index++) {
    if (results[index]) {
      Serial.print("Found device ");
      Serial.print(index);
      Serial.print(" at address ");
      Serial.println(addresses[index], HEX);
    }
  }
  delay(5000);

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ; // Don't proceed, loop forever
  }

  // Show initial display buffer contents on the screen --
  // the library initializes this with an Adafruit splash screen.
  display.display();
  delay(2000); // Pause for 2 seconds

  // Clear the buffer
  display.clearDisplay();

  Serial.println("Running arduino project v0.1.6 ...");

  // initialize the RTC
  rtc.init();

  // test if clock is halted and set a date-time (see example 2) to start it
  if (rtc.isHalted()) {
    Serial.println("RTC is halted. Setting time...");

    Ds1302::DateTime dt = {.year = 25,
                           .month = Ds1302::MONTH_SEP,
                           .day = 27,
                           .hour = 2,
                           .minute = 44,
                           .second = 30,
                           .dow = Ds1302::DOW_SAT};

    rtc.setDateTime(&dt);
  }

  // Show initial display buffer contents on the screen --
  // the library initializes this with an Adafruit splash screen.
  display.display();
  delay(2000); // Pause for 2 seconds

  // Clear the buffer
  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 32);
  // Display static text
  display.println("Hello, world!");
  display.display();

  delay(10000);

  // Draw a single pixel in white
  for (int i = 0; i < SCREEN_WIDTH; i++) {
    for (int j = 0; j < SCREEN_HEIGHT; j++) {
      display.drawPixel(i, j, SSD1306_WHITE);
    }
  }

  // Show the display buffer on the screen. You MUST call display() after
  // drawing commands to make them visible on screen!
  display.display();
  delay(5000);

  testdrawbitmap();

  display.invertDisplay(true);
  delay(1000);
  display.invertDisplay(false);
  delay(1000);

  testdrawbitmap();
  delay(2000);
  display.clearDisplay();
}

// Loop function: Runs repeatedly
void loop() {
  delay(250);

  rtc.getDateTime(&now);

  static uint8_t last_second = 0;
  if (last_second != now.second) {
    last_second = now.second;

    Serial.print("20");
    Serial.print(now.year); // 00-99
    Serial.print('-');
    if (now.month < 10) {
      Serial.print('0');
    }
    Serial.print(now.month); // 01-12
    Serial.print('-');
    if (now.day < 10) {
      Serial.print('0');
    }
    Serial.print(now.day); // 01-31
    Serial.print(' ');
    Serial.print(WeekDays[now.dow - 1]); // 1-7
    Serial.print(' ');
    if (now.hour < 10) {
      Serial.print('0');
    }
    Serial.print(now.hour); // 00-23
    Serial.print(':');
    if (now.minute < 10) {
      Serial.print('0');
    }
    Serial.print(now.minute); // 00-59
    Serial.print(':');
    if (now.second < 10) {
      Serial.print('0');
    }
    Serial.print(now.second); // 00-59
    Serial.println();

    display.clearDisplay();

    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.print("20");
    display.print(now.year); // 00-99
    display.print('-');
    if (now.month < 10) {
      display.print('0');
    }
    display.print(now.month); // 01-12
    display.print('-');
    if (now.day < 10) {
      display.print('0');
    }
    display.print(now.day); // 01-31

    display.setCursor(0, 26);
    display.print(WeekDays[now.dow - 1]); // 1-7

    display.setCursor(0, 48);

    // 00-23
    int hour = now.hour;
    if (hour == 0) {
      hour = 12;
    } else if (hour >= 12) {
      hour = hour - 12;
      if (hour <= 0) {
        hour = 12;
      }
    }
    display.print(hour);

    display.print(':');
    if (now.minute < 10) {
      display.print('0');
    }
    display.print(now.minute); // 00-59
    display.print(':');
    if (now.second < 10) {
      display.print('0');
    }
    display.print(now.second); // 00-59

    if (now.hour > 11) {
      display.print("PM");
    } else {
      display.print("AM");
    }

    display.display();
  }
}
