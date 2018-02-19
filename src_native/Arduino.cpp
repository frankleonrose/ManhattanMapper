#include "Arduino.h"

MockSerial Serial;
MockSerial Serial1;

void delay(uint16_t msec) {};
void delayMicroseconds(uint32_t usec) {};
uint32_t micros() { return 0; };
void interrupts() {};
void noInterrupts() {};

void pinMode(uint8_t pin, uint8_t mode) {};
void digitalWrite(uint8_t pin, uint8_t val) {};
int digitalRead(uint8_t pin) { return 0; };
