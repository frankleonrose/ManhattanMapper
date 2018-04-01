#include <Arduino.h>
#include <SPI.h>
#include <SdFat.h>
#include <ParameterStore.h>
#include <Logging.h>
#include "mm_state.h"

#define SD_CARD_CS 10

extern AppState gState;
extern RespireContext<AppState> gRespire;
static bool gSDAvailable = false;

static char *kParamFile = "params.ini";

// https://learn.adafruit.com/using-atsamd21-sercom-to-add-more-spi-i2c-serial-ports/creating-a-new-spi
// http://asf.atmel.com/docs/3.27.0/samd21/html/asfdoc_sam0_sercom_spi_mux_settings.html
// 13 - clock (3:1), 12 - MOSI (3:3), 11 - MISO (3:0)
SPIClass MM_SD_SPI (&sercom3, 11 /*MISO*/, 13/*CLK*/, 12/*MOSI*/, SPI_PAD_3_SCK_1, SERCOM_RX_PAD_0);
// Hooked into SdFat by -DSDCARD_SPI=MM_SD_SPI and `extern SPIClass MM_SD_SPI;` statement in SdSpiDriver.h

SdFat SD;

bool readParametersFromSD(ParameterStore &pstore) {
  if (gSDAvailable && SD.exists(kParamFile)) {
    File file = SD.open(kParamFile, FILE_READ);
    if (file) {
      size_t size = file.size();
      if (size>4000) {
        Log.Error(F("Size of parameter file '%s' (%d) is too big!\n"), kParamFile, size);
        file.close();
        return false;
      }
      char buffer[size];
      int res = file.readBytes(buffer, size);
      file.close();
      if (res!=-1) {
        Log.Error(F("Could not read entirety of parameter file '%s'.\n"), kParamFile);
        return false;
      }
      bool ok = pstore.deserialize(buffer, size);
      return ok;
    }
    else {
      Log.Error(F("Could not open parameter file '%s'.\n"), kParamFile);
      return false;
    }
  }
  else {
    Log.Debug("No parameter file '%s' to read.\n", kParamFile);
    return true;
  }
}

bool writeParametersToSD(ParameterStore &pstore) {
  if (!gSDAvailable) {
    return false;
  }

  SD.remove(kParamFile); // TODO: Should perform write to dummy followed by rename to actual one. Alternatively, alternate writing to different ones.

  char buffer[2000];
  int size = pstore.serialize(buffer, sizeof(buffer));

  if (size<0) {
    Log.Error(F("Failed to serialize parameter store.\n"));
    return false;
  }

  File file = SD.open(kParamFile, FILE_WRITE);
  if (file) {
    size_t written = file.write(buffer, size);
    file.close();
    if (written!=size) {
      Log.Error(F("Could not read entirety of parameter file '%s'.\n"), kParamFile);
      return false;
    }
    return true;
  }
  else {
    Log.Error(F("Could not open parameter file '%s' for writing parameters.\n"), kParamFile);
    return false;
  }
}

bool makePath(char *filename) {
  bool success = true;
  for (char *sep = filename; success && (sep = strstr(sep, "/"))!=NULL; ++sep) {
    if (sep==filename) {
      continue; // No need to create root path /
    }
    *sep = '\0'; // Terminate at separator
    if (!SD.exists(filename)) {
      // Create if doesn't exist
      Log.Debug("Path \"%s\" does not exist\n", filename);
      if (!SD.mkdir(filename)) {
        Log.Error("Failed to create path \"%s\"\n", filename);
        success = false;
      }
    }
    *sep = '/'; // Restore separator and check next path segment Ok
  }
  return true;
}

void writeLocation(const AppState &state, const AppState &oldState, Mode *triggeringMode) {
  if (!gSDAvailable) {
    Log.Debug("Completing %s\n", triggeringMode->name());
    gRespire.complete(triggeringMode);
    return;
  }

  char filename[300];
  const GpsSample &gps = state.gpsSample();

  sprintf(filename, "/gps/%04d/%02d/%02d/%02d.csv", (int)gps._year, (int)gps._month, (int)gps._day, (int)gps._hour);
  if (!makePath(filename)) {
    Log.Error("Failed to makePath %s\n", filename);
    gRespire.complete(triggeringMode);
    return;
  }

  char dataString[300];
  sprintf(dataString, "%04d%02d%02d:%02d%02d%02d.%03d,%f,%f,%f,%f,battery,frame", // TODO frame & battery
        (int)gps._year, (int)gps._month, (int)gps._day, (int)gps._hour, (int)gps._minute, (int)gps._seconds, (int)gps._millis,
        gps._latitude, gps._longitude, gps._altitude, gps._HDOP /*, state.ttnFrameUp(), state.batteryLevel() */);

  Log.Debug("Writing \"%s\" to file \"%s\"\n", dataString, filename);

  File dataFile = SD.open(filename, FILE_WRITE);
  Log.Debug("Opened %s\n", filename);
  if (dataFile) {
    dataFile.println(dataString);
    Log.Debug("Wrote %s\n", filename);
    dataFile.close();
    Log.Debug("Closed %s\n", filename);
  }
  else {
    Log.Error("Error opening %s\n", triggeringMode->name());
  }
  Log.Debug("Completing %s\n", triggeringMode->name());
  gRespire.complete(triggeringMode);
}

void storageSetup() {
  if (!SD.begin(SD_CARD_CS, SPISettings(1000000, MSBFIRST, SPI_MODE0))) {
    Log.Error("Card failed or not present\n");
    // while (1);
    gSDAvailable = false;
  }
  else {
    Log.Debug("SD card interface initialized.\n");
    gSDAvailable = true;
  }
}