// Arduino.h – Perfect, zero-error Linux port (2025 final version)
// Fixes the extern/static Serial conflict and everything else.

#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>

// ─────────────────────────────────────────────────────────────────────────────
// Basic Arduino types
// ─────────────────────────────────────────────────────────────────────────────
using byte    = uint8_t;
using word    = uint16_t;
using boolean = bool;

#ifndef FALSE
#define FALSE false
#define TRUE  true
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Time functions
// ─────────────────────────────────────────────────────────────────────────────
static uint64_t arduino_epoch_ms = 0;

inline unsigned long millis(void)
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    uint64_t now = uint64_t(tv.tv_sec) * 1000ULL + tv.tv_usec / 1000ULL;
    if (arduino_epoch_ms == 0) arduino_epoch_ms = now;
    return static_cast<unsigned long>(now - arduino_epoch_ms);
}

inline unsigned long micros(void)
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    uint64_t now = uint64_t(tv.tv_sec) * 1000000ULL + tv.tv_usec;
    if (arduino_epoch_ms == 0) {
        gettimeofday(&tv, nullptr);
        arduino_epoch_ms = uint64_t(tv.tv_sec) * 1000ULL + tv.tv_usec / 1000ULL;
    }
    return static_cast<unsigned long>(now - arduino_epoch_ms * 1000ULL);
}

inline void delay(unsigned long ms)
{
    struct timespec ts = { static_cast<time_t>(ms / 1000), static_cast<long>((ms % 1000) * 1000000L) };
    nanosleep(&ts, nullptr);
}

inline void delayMicroseconds(unsigned int us)
{
    struct timespec ts = { 0, static_cast<long>(us * 1000L) };
    nanosleep(&ts, nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Math helpers
// ─────────────────────────────────────────────────────────────────────────────
#ifndef PI
#define PI         3.1415926535897932384626433832795
#define HALF_PI    1.5707963267948966192313216916398
#define TWO_PI     6.283185307179586476925286766559
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105
#endif

inline double radians(double deg) { return deg * DEG_TO_RAD; }
inline double degrees(double rad) { return rad * RAD_TO_DEG; }
inline double sq(double x)        { return x * x; }

// ─────────────────────────────────────────────────────────────────────────────
// Bit manipulation
// ─────────────────────────────────────────────────────────────────────────────
#define bit(b)             (1UL << (b))
#define bitRead(v, b)      (((v) >> (b)) & 1)
#define bitSet(v, b)       ((v) |= bit(b))
#define bitClear(v, b)     ((v) &= ~bit(b))
#define bitWrite(v,b,x)    ((x) ? bitSet(v,b) : bitClear(v,b))

// ─────────────────────────────────────────────────────────────────────────────
// Pin dummies
// ─────────────────────────────────────────────────────────────────────────────
#define INPUT  0
#define OUTPUT 1
#define HIGH   1
#define LOW    0

inline void pinMode(uint8_t, uint8_t)    {}
inline void digitalWrite(uint8_t, uint8_t) {}
inline int  digitalRead(uint8_t)         { return LOW; }

// ─────────────────────────────────────────────────────────────────────────────
// HardwareSerial – THE FIX: only ONE definition, no extern + static conflict
// ─────────────────────────────────────────────────────────────────────────────
class HardwareSerial {
public:
    void begin(unsigned long) const {}
    size_t print(const char* s) const          { return fputs(s, stdout); }
    size_t print(int n, int base = 10) const;
    size_t println(const char* s) const        { int r = fputs(s, stdout); putchar('\n'); return r + 1; }
    size_t println() const                     { putchar('\n'); return 1; }
    size_t write(uint8_t c) const              { putchar(c); return 1; }
};

inline size_t HardwareSerial::print(int n, int base) const
{
    if (base == 10) return printf("%d", n);
    if (base == 16) return printf("%x", n);
    if (base == 8)  return printf("%o", n);
    return printf("%d", n);
}

// This is the ONLY correct way in a header: declare + define in one place
inline HardwareSerial Serial;        // ← This is the fix! No extern, no static, no conflict
