#ifndef ARDUINO_H
#define ARDUINO_H

// Mock Arduino.h used when compiling native platform tests.

#include <string>
#include <cstdint>
#include <cstring>
#include <string.h>
#include <cstdio>

#define String std::string

#define isDigit isdigit
#define isAlpha isalpha

#include <Stream.h>

#define F_CPU 100000000

#define F(s) (s)
class __FlashStringHelper;

#define LED_BUILTIN 13

#define LOW   0
#define HIGH  1

#define INPUT   0
#define OUTPUT  1

typedef unsigned char   boolean;
typedef unsigned char   uint8_t;
typedef unsigned short  uint16_t;
typedef unsigned int    uint32_t;

#ifdef __cplusplus
extern "C" {
#endif
void delay(uint16_t msec);
void delayMicroseconds(uint32_t usec);
unsigned long millis();
uint32_t micros();
void interrupts();
void noInterrupts();

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t val);
int digitalRead(uint8_t pin);

#ifdef __cplusplus
}
#endif

template<class T> T constrain(const T value, const T min, const T max) {
  if (value<min) {
    return min;
  }
  else if (value>max) {
    return max;
  }
  else {
    return value;
  }
}

#ifndef UNIT_TEST
float fmod(float numer, float denom) throw() {
  return numer;
  // return numer - (ftrunc(numer / denom) * denom);
}
#endif

#endif
