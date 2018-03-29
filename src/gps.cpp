// https://github.com/adafruit/Adafruit_GPS/blob/master/examples/GPS_HardwareSerial_Parsing/GPS_HardwareSerial_Parsing.ino

#ifndef UNIT_TEST

#include <Adafruit_GPS.h>
#include <Arduino.h>
#include <Logging.h>
#include <Adafruit_ZeroTimer.h>
#include <ParameterStore.h> // For htonl htons

#include "gps.h"

#define ELEMENTS(_array) (sizeof(_array) / sizeof(_array[0]))

#define GPS_FIX_PIN 12
#define GPS_ENABLE_PIN 11

HardwareSerial &gpsSerial = Serial1;
Adafruit_GPS GPS(&gpsSerial);
std::function< void(const GpsSample &gpsSample) > gReadSuccess;
std::function< void(void) > gReadFailure;

// Set GPSECHO to 'false' to turn off echoing the GPS data to the Serial console
// Set to 'true' if you want to debug and listen to the raw GPS sentences.
#define GPSECHO false

static volatile bool gpsHasFixStatus = false;
static uint8_t gpsFixIndex = 0;
static bool gpsFixHistory[5];
static Adafruit_ZeroTimer gpsFixTimer = Adafruit_ZeroTimer(4);

void gpsFixISR(struct tc_module *const module_inst) {
  // Used to distinguish between 2 signals
  // No GPS Fix: ____----____----____----____----____----____----         (1s low, 1s high)
  // Yes GPS Fix: __________-____________________________-________________ (15s low, 200ms high)
  // Called every 350ms, fill a buffer of 5 samples. If two of them are high, it's the first signal.
  // Period must be:
  //   p < 500 so that a high signal for 1 second will always yield two high samples
  //   p > 333 so that if we start sampling at low start, the 4th & 5th samples will be in the high phase.
  //   Prefer a shorter period so that 5 cycles happen more quickly and we recognize our state.
  //   I choose 350ms to give a little wiggle room. The GPS clock is ns accurate, but our mcu timer is not.
  //   Worst case, it'll take 5 * 350ms (1.75s) to figure out we lost fix. 1s to figure out we acheived fix.

  // static bool ledFlash = false;
  // digitalWrite(LED_BUILTIN, (ledFlash = !ledFlash));

  bool sample = digitalRead(GPS_FIX_PIN);
  gpsFixHistory[gpsFixIndex] = sample;
  gpsFixIndex = ++gpsFixIndex % ELEMENTS(gpsFixHistory);

  uint8_t countHigh = 0;
  for (uint8_t i=0; i<ELEMENTS(gpsFixHistory); ++i) {
    if (gpsFixHistory[i]) {
      ++countHigh;
    }
  }
  gpsHasFixStatus = (countHigh <= 1);
}

bool gpsHasFix() {
  return gpsHasFixStatus;
}

void gpsEnable(bool enable) {
  Log.Debug("Setting GPS enable: %T\n", enable);
  digitalWrite(GPS_ENABLE_PIN, !enable);
}

void gpsSetup()
{

  Log.Debug("gpsSetup begin\n");
  // 9600 NMEA is the default baud rate for Adafruit MTK GPS's- some use 4800
  GPS.begin(9600);

  // uncomment this line to turn on RMC (recommended minimum) and GGA (fix data) including altitude
  GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA);
  // uncomment this line to turn on only the "minimum recommended" data
  //GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCONLY);
  // For parsing data, we don't suggest using anything but either RMC only or RMC+GGA since
  // the parser doesn't care about other sentences at this time

  // Set the update rate
  GPS.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ);   // 1 Hz update rate
  // For the parsing code to work nicely and have time to sort thru the data, and
  // print it out we don't suggest using anything higher than 1 Hz

  // Request updates on antenna status or explicitly not
  // GPS.sendCommand(PGCMD_ANTENNA);
  GPS.sendCommand(PGCMD_NOANTENNA);

  delay(1000);
  // Ask for firmware version
  GPS.sendCommand(PMTK_Q_RELEASE);


  pinMode(GPS_ENABLE_PIN, OUTPUT);
  digitalWrite(GPS_ENABLE_PIN, HIGH); // Disabled initially

  pinMode(GPS_FIX_PIN, INPUT);

  gpsHasFixStatus = false;
  gpsFixIndex = 0;
  for (uint8_t i=0; i<ELEMENTS(gpsFixHistory); ++i) {
    gpsFixHistory[i] = false;
  }

  Log.Debug("gpsSetup setup fix timer\n");

  gpsFixTimer.configure(TC_CLOCK_PRESCALER_DIV1024, // prescaler: 48000(m0 clock freq) / 1024(prescaler) = 46.875kHz
                TC_COUNTER_SIZE_16BIT,            // bit width of timer/counter (avoid 32 bit because that uses two timers!)
                TC_WAVE_GENERATION_MATCH_FREQ     // match style
                );

  gpsFixTimer.setPeriodMatch(16406, 1, 0); // 350ms period = 2.857Hz = 46.875k / 16406, 1 match, 0 channel
  gpsFixTimer.setCallback(true, TC_CALLBACK_CC_CHANNEL0, gpsFixISR);
  Log.Debug("gpsSetup enable fix timer\n");
  gpsFixTimer.enable(true);
  Log.Debug("gpsSetup done\n");
}

void gpsLoop(Print &printer)
{
  char c = GPS.read();
  // if you want to debug, this is a good time to do it!
  if (GPSECHO)
    if (c) printer.print(c);

  // if a sentence is received, we can check the checksum, parse it...
  if (GPS.newNMEAreceived()) {
    char *gpsInput = GPS.lastNMEA(); // Resets the newNMEAreceived() flag to false
    // Log.Debug("GPS NMEA string: %s\n", gpsInput);
    if (gReadSuccess && strstr(gpsInput, "$GPGGA")!=NULL && GPS.year!=0) {
      // We are interested in a new location
      if (GPS.parse(gpsInput)) {
        GpsSample sample(GPS.latitudeDegrees, GPS.longitudeDegrees, GPS.altitude, GPS.HDOP, 2000 + GPS.year, GPS.month, GPS.day, GPS.hour, GPS.minute, GPS.seconds, GPS.milliseconds);
        gReadSuccess(sample);
      }
      else if (gReadFailure) {
        gReadFailure();
      }
      gReadSuccess = NULL;
      gReadFailure = NULL;
    }
    else {
      if (!GPS.parse(gpsInput)) {
        Log.Warn("Failed to parse GPS string \"%s\"\n", gpsInput);
      }
    }
  }
}

void gpsRead(std::function< void(const GpsSample &gpsSample) > success, std::function< void() > failure) {
  gReadSuccess = success;
  gReadFailure = failure;
  GPS.lastNMEA(); // Reset last reading
}

void gpsDump(Print &printer) {
  printer.print("Date: 20");
  printer.print(GPS.year, DEC); printer.print('-');
  printer.print(GPS.month, DEC); printer.print('-');
  printer.println(GPS.day, DEC);

  printer.print("Time: ");
  printer.print(GPS.hour, DEC); printer.print(':');
  printer.print(GPS.minute, DEC); printer.print(':');
  printer.print(GPS.seconds, DEC); printer.print('.');
  printer.println(GPS.milliseconds);

  printer.print("Fix: "); printer.print((int)GPS.fix);
  printer.print(" quality: "); printer.println((int)GPS.fixquality);
  if (GPS.fix) {
    printer.print("Location: (dddmm.ss)");
    printer.print(GPS.latitude, 4); printer.print(GPS.lat);
    printer.print(", ");
    printer.print(GPS.longitude, 4); printer.println(GPS.lon);
    printer.print("Location (degrees): ");
    printer.print(GPS.latitudeDegrees, 4);
    printer.print(", ");
    printer.println(GPS.longitudeDegrees, 4);

    printer.print("Speed (knots): "); printer.println(GPS.speed);
    printer.print("Angle: "); printer.println(GPS.angle);
    printer.print("Altitude: "); printer.println(GPS.altitude);
    printer.print("Satellites: "); printer.println((int)GPS.satellites);
  }
}

uint8_t GpsSample::writePacket(uint8_t *packet, uint8_t packetSize) const {
  int32_t lat = _latitude * 46603;  // Expand +/-180 coordinate to fill 24bits
  uint32_t ulat = lat < 0 ? (UINT32_MAX-(uint32_t)(-lat)+1) : (uint32_t)(lat);
  ulat = htonl(ulat);
  int32_t lon = _longitude * 93206; // Expand +/-90 coordinate to fill 24bits
  uint32_t ulon = lon < 0 ? (UINT32_MAX-(uint32_t)(-lon)+1) : (uint32_t)(lon);
  ulon = htonl(ulon);
  int16_t alt = _altitude;
  alt = htons(alt);
  int16_t hdop = _HDOP * 1000;
  hdop = htons(hdop);

  memcpy(packet + 0, &ulat, 3); // 24 bit
  memcpy(packet + 3, &ulon, 3); // 24 bit
  memcpy(packet + 6,  &alt, 2);
  memcpy(packet + 8, &hdop, 2);

  return 10;
}

#endif