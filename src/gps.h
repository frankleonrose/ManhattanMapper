#include <Arduino.h>

class Adafruit_GPS;

void gpsSetup();
void gpsLoop(Print &printer);
void gpsDump(Print &printer);
void gpsRead(void (*success)(const Adafruit_GPS &gps), void (*failure)());
