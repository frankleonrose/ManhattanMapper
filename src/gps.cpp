// https://github.com/adafruit/Adafruit_GPS/blob/master/examples/GPS_HardwareSerial_Parsing/GPS_HardwareSerial_Parsing.ino

#ifndef UNIT_TEST

#include <Adafruit_GPS.h>
#include <Arduino.h>
#include <Logging.h>
#include <Adafruit_ZeroTimer.h>
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

static volatile bool gpsHasFixStatus;
bool gpsHasFix() {
  return gpsHasFixStatus;
}

void gpsEnable(bool enable) {
  Log.Debug("Setting GPS enable: %T", enable);
  digitalWrite(GPS_ENABLE_PIN, !enable);
}

void gpsSetup()
{

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

  // Request updates on antenna status, comment out to keep quiet
  GPS.sendCommand(PGCMD_NOANTENNA);

  delay(1000);
  // Ask for firmware version
  GPS.sendCommand(PMTK_Q_RELEASE);


  pinMode(GPS_ENABLE_PIN, OUTPUT);
  digitalWrite(GPS_ENABLE_PIN, HIGH); // Disabled initially

  gpsHasFixStatus = false;
}

void gpsLoop(Print &printer)
{
  char c = GPS.read();
  // if you want to debug, this is a good time to do it!
  if (GPSECHO)
    if (c) printer.print(c);

  // if a sentence is received, we can check the checksum, parse it...
  if (GPS.newNMEAreceived()) {
    if (gReadSuccess) {
      Log.Debug("GPS string: %s\n", GPS.lastNMEA());
      // We are interested in a new location
      if (GPS.parse(GPS.lastNMEA())) {   // this also sets the newNMEAreceived() flag to false
        GpsSample sample(GPS.latitudeDegrees, GPS.longitudeDegrees, GPS.altitude, GPS.HDOP, 2000 + GPS.year, GPS.month, GPS.day, GPS.hour, GPS.minute, GPS.seconds, GPS.milliseconds);
        gReadSuccess(sample);
      }
      else if (gReadFailure) {
        gReadFailure();
      }
      gReadSuccess = NULL;
      gReadFailure = NULL;
    }
  }
}

void gpsRead(std::function< void(const GpsSample &gpsSample) > success, std::function< void() > failure) {
  gReadSuccess = success;
  gReadFailure = failure;
  GPS.lastNMEA(); // Reset last reading
}

void gpsDump(Print &printer) {
  // printer.print("Date: 20");
  // printer.print(GPS.year, DEC); printer.print('-');
  // printer.print(GPS.month, DEC); printer.print('-');
  // printer.println(GPS.day, DEC);

  // printer.print("\nTime: ");
  // printer.print(GPS.hour, DEC); printer.print(':');
  // printer.print(GPS.minute, DEC); printer.print(':');
  // printer.print(GPS.seconds, DEC); printer.print('.');
  // printer.println(GPS.milliseconds);

  // printer.print("Fix: "); printer.print((int)GPS.fix);
  // printer.print(" quality: "); printer.println((int)GPS.fixquality);
  // if (GPS.fix) {
  //   printer.print("Location: (dddmm.ss)");
  //   printer.print(GPS.latitude, 4); printer.print(GPS.lat);
  //   printer.print(", ");
  //   printer.print(GPS.longitude, 4); printer.println(GPS.lon);
  //   printer.print("Location (degrees): ");
  //   printer.print(GPS.latitudeDegrees, 4);
  //   printer.print(", ");
  //   printer.println(GPS.longitudeDegrees, 4);

  //   printer.print("Speed (knots): "); printer.println(GPS.speed);
  //   printer.print("Angle: "); printer.println(GPS.angle);
  //   printer.print("Altitude: "); printer.println(GPS.altitude);
  //   printer.print("Satellites: "); printer.println((int)GPS.satellites);
  // }
}

#endif