#include "Arduino.h"

MockSerial Serial;
MockSerial Serial1;

void delay(uint16_t msec) {};
void delayMicroseconds(uint32_t usec) {};
unsigned long millis() { return 0; };
uint32_t micros() { return 0; };
void interrupts() {};
void noInterrupts() {};

void pinMode(uint8_t pin, uint8_t mode) {};
void digitalWrite(uint8_t pin, uint8_t val) {};
int digitalRead(uint8_t pin) { return 0; };

size_t Print::print(const char value[]) {
  return printf("%s", value);
}
size_t Print::print(char value) {
  return printf("%c", value);
}
size_t Print::print(unsigned char value, int base) {
  return printf(base==HEX ? "%x" : "%u", (unsigned)value);;
}
size_t Print::print(int value, int base) {
  return printf(base==HEX ? "%x" : "%d", (int)value);;
}
size_t Print::print(unsigned int value, int base) {
  return printf(base==HEX ? "%x" : "%u", (unsigned)value);;
}
size_t Print::print(long value, int base) {
  return printf(base==HEX ? "%x" : "%d", (int)value);;
}
size_t Print::print(unsigned long value, int base) {
  return printf(base==HEX ? "%lx" : "%lu", value);;
}
size_t Print::print(double value, int base) {
  return printf("%lf", value);
}
