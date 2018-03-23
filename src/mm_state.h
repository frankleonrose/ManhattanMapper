/*
  Herein I attempt to imagine what the Helex-generated code might look like.

  AppState - The entire application state.
  Mutator - A mutation of the app state. (Right now just setABC() calls.)
  OnTime - Tickle function that gets called as time passes. In the future
          this should all be done with scheduling such that the system knows
          exactly when next it cares to do something.
  Mode - The basic organizational unit of Helex. The structure and attributes
          of a particular Mode are stored in the Mode itself.
          The runtime state of all Modes is stored within ModeState structs
          that are contained within the AppState object. This breakdown
          allows the entire application state to be copied simply with
          memcpy. (Not that we do, but, for instance, the way Modes have
          constant addresses that can be used to refer to them, as opposed
          to being members of AppState.)
 */

#include <Arduino.h>
#include <limits.h>
#include <cstdio>
#include <cassert>
#undef min
#undef max
#include <vector>
#include <algorithm>
#include "respire.h"
#include <Logging.h>

extern void changeGpsPower(const AppState &state, const AppState &oldState);
extern void readGpsLocation(const AppState &state, const AppState &oldState);
extern void attemptJoin(const AppState &state, const AppState &oldState);
extern void changeSleep(const AppState &state, const AppState &oldState);
extern void sendLocation(const AppState &state, const AppState &oldState);
extern void sendLocationAck(const AppState &state, const AppState &oldState);
extern void writeLocation(const AppState &state, const AppState &oldState);

// Modes
extern Mode ModeMain;
extern Mode ModeSleep;
extern Mode ModeAttemptJoin;
extern Mode ModeLowPowerJoin;
extern Mode ModeLowPowerGpsSearch;
extern Mode ModeLowPowerSend;
extern Mode ModePeriodicJoin;
extern Mode ModePeriodicSend;
extern Mode ModeReadAndSend;
extern Mode ModeReadGps;
extern Mode ModeSend;
extern Mode ModeSendNoAck;
extern Mode ModeSendAck;
extern Mode ModeLogGps;

#define SAMPLE_VALID_FOR_MS 2000

class AppState : public RespireState<AppState> {
  // External state
  bool _usbPower;
  bool _gpsFix;

  float _latitude;
  float _longitude;
  float _altitude;
  float _HDOP;
  uint32_t _gpsSampleExpiry;

  // Dependent state - no setters
  bool _joined;
  bool _gpsPowerOut;

  public:
  AppState() {
    reset();
  }

  AppState(const AppState& otherState)
  : RespireState(otherState),
    _usbPower(otherState._usbPower),
    _gpsFix(otherState._gpsFix),
    _joined(otherState._joined),
    _gpsPowerOut(otherState._gpsPowerOut),
    _latitude(otherState._latitude),
    _longitude(otherState._longitude),
    _altitude(otherState._altitude),
    _HDOP(otherState._HDOP),
    _gpsSampleExpiry(otherState._gpsSampleExpiry)
  {}

  void reset() {
    RespireState<AppState>::reset();
    _usbPower = false;
    _gpsFix = false;
    _joined = false;
    _gpsPowerOut = false;
    _gpsSampleExpiry = 0;
  }

  // USB power
  bool getUsbPower() const {
    return _usbPower;
  }

  void setUsbPower(bool value) {
    if (_usbPower==value) {
      // Short circuit no change
      return;
    }
    AppState oldState(*this);
    _usbPower = value;
    onUpdate(oldState);
  }

  bool hasGpsFix() const {
    return _gpsFix;
  }

  void setGpsFix(bool value) {
    if (_gpsFix==value) {
      // Short circuit no change
      return;
    }
    AppState oldState(*this);
    _gpsFix = value;
    onUpdate(oldState);
  }

  void setGpsLocation(float latitude, float longitude, float altitude, float HDOP) {
    Log.Debug("setGpsLocation -----------------------------\n");
    AppState oldState(*this);
    _latitude = latitude;
    _longitude = longitude;
    _altitude = altitude;
    _HDOP = HDOP;
    _gpsSampleExpiry = millis() + SAMPLE_VALID_FOR_MS;
    onUpdate(oldState);
  }

  bool hasGpsLocation() const {
    return _gpsSampleExpiry!=0 && (millis() < _gpsSampleExpiry);
  }

  bool getJoined() const {
    return _joined;
  }

  void setJoined(bool value) {
    if (_joined==value) {
      // Short circuit no change
      return;
    }
    AppState oldState(*this);
    _joined = value;
    onUpdate(oldState);
  }

  bool getGpsPower() const {
    return _gpsPowerOut;
  }

  void dump() const {
    Log.Debug("AppState: ----------------\n");
    Log.Debug("- Millis:             %u\n", (long unsigned)millis());
    Log.Debug("- Counter:            %u\n", (long unsigned)changeCounter());
    Log.Debug("- USB Power [Input]:  %T\n", _usbPower);
    Log.Debug("- Joined [Input]:     %T\n", _joined);
    Log.Debug("- GPS Power [Output]: %T\n", _gpsPowerOut);
    Log.Debug("- GPS Fix [Input]:     %T\n", _gpsFix);
    Log.Debug("- GPS Location [Input]: %T\n", hasGpsLocation());
    Log.Debug("- GPS Expiry [Input]: %u\n", _gpsSampleExpiry);
    Log.Debug("- GPS Latitude, Longitude, Altitude, HDOP [Input]: %f, %f, %f, %f\n", _latitude, _longitude, _altitude, _HDOP);
    ModeMain.dump(*this);
    Log.Debug("AppState: ---------------- END\n");
  }

  virtual void onChange(const AppState &oldState, Executor *executor) {
    _gpsPowerOut = _usbPower || ModeLowPowerGpsSearch.isActive(*this);

    // This should be simple listener or output transducer
    if (_gpsPowerOut!=oldState._gpsPowerOut) {
      executor->exec(changeGpsPower, *this, oldState, NULL);
    }
  }
};
