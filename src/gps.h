#include <Arduino.h>
#include "mm_state.h"
#include <functional>

class Adafruit_GPS;

void gpsSetup();
void gpsLoop(Print &printer);
bool gpsHasFix();
void gpsEnable(bool enable);
void gpsDump(Print &printer);
void gpsRead(std::function< void(const GpsSample &gpsSample) > success,  std::function< void(void) >failure);
