/*
  Herein I attempt to imagine what the Respire-generated code might look like.

  AppState - The entire application state.
  Mutator - A mutation of the app state. (Right now just setABC() calls.)
  OnTime - Tickle function that gets called as time passes. In the future
          this should all be done with scheduling such that the system knows
          exactly when next it cares to do something.
  Mode - The basic organizational unit of Respire. The structure and attributes
          of a particular Mode are stored in the Mode itself.
          The runtime state of all Modes is stored within ModeState structs
          that are contained within the AppState object. This breakdown
          allows the entire application state to be copied simply with
          memcpy. (Not that we do, but, for instance, the way Modes have
          constant addresses that can be used to refer to them, as opposed
          to being members of AppState.)
 */

#ifndef MM_STATE_H
#define MM_STATE_H

#include <Arduino.h>
#include <limits.h>
#include <cstdio>
#include <cassert>
#undef min
#undef max
#include <vector>
#include <algorithm>
#include <cmath>
#include "respire.h"
#include <Logging.h>

#define FLOAT_SAME(a, b, precision) (fabs((a) - (b)) < precision)

class AppState;

extern void changeGpsPower(const AppState &state, const AppState &oldState, Mode<AppState> *triggeringMode);
extern void readGpsLocation(const AppState &state, const AppState &oldState, Mode<AppState> *triggeringMode);
extern void attemptJoin(const AppState &state, const AppState &oldState, Mode<AppState> *triggeringMode);
extern void changeSleep(const AppState &state, const AppState &oldState, Mode<AppState> *triggeringMode);
extern void sendLocation(const AppState &state, const AppState &oldState, Mode<AppState> *triggeringMode);
extern void sendLocationAck(const AppState &state, const AppState &oldState, Mode<AppState> *triggeringMode);
extern void writeLocation(const AppState &state, const AppState &oldState, Mode<AppState> *triggeringMode);

extern void displayBlank(const AppState &state, const AppState &oldState, Mode<AppState> *triggeringMode);
extern void displayStatus(const AppState &state, const AppState &oldState, Mode<AppState> *triggeringMode);
extern void displayParameters(const AppState &state, const AppState &oldState, Mode<AppState> *triggeringMode);
extern void displayErrors(const AppState &state, const AppState &oldState, Mode<AppState> *triggeringMode);

// Modes
extern Mode<AppState>
  ModeMain,
  ModeDisplay,
  ModeDisplayBlank,
  ModeDisplayBlank2,
  ModeDisplayStatus,
  ModeDisplayParameters,
  ModeDisplayErrors,
  ModeFunctional,
  ModeSleep,
  ModeAttemptJoin,
  ModeLowPowerJoin,
  ModeLowPowerGpsSearch,
  ModeLowPowerSend,
  ModePeriodicJoin,
  ModePeriodicSend,
  ModeReadAndSend,
  ModeReadGps,
  ModeSend,
  ModeSendNoAck,
  ModeSendAck,
  ModeLogGps,
  ModeRejoinAfterAck;

#define SAMPLE_VALID_FOR_MS 2000

typedef struct GpsSample {
  float _latitude = 0.0;
  float _longitude = 0.0;
  float _altitude = 0.0;
  float _HDOP = 0.0;
  uint16_t _year = 0;
  uint8_t _month = 0, _day = 0, _hour = 0, _minute = 0, _seconds = 0;
  uint16_t _millis = 0;

  GpsSample() {};

  GpsSample(float latitude, float longitude, float altitude, float HDOP, uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t seconds, uint16_t millis)
  : _latitude(latitude),
    _longitude(longitude),
    _altitude(altitude),
    _HDOP(HDOP),
    _year(year),
    _month(month),
    _day(day),
    _hour(hour),
    _minute(minute),
    _seconds(seconds),
    _millis(millis)
  {}

  uint8_t writePacket(uint8_t *packet, uint8_t packetSize) const;

  void dump() const {
    Log.Debug("- GPS Latitude, Longitude, Altitude, HDOP [Input]: %f, %f, %f, %f\n", _latitude, _longitude, _altitude, _HDOP);
  }
} GpsSample;

extern uint8_t fieldCountForPage(const AppState &state, uint8_t page);

class AppState : public RespireState<AppState> {
  // External state
  bool _usbPower = false;
  float _batteryVolts = 0.0;
  bool _gpsFix = false;

  GpsSample _gpsSample;
  uint32_t _gpsSampleExpiry = 0;

  uint32_t _ttnFrameCounter = 0;
  uint32_t _ttnLastSend = 0;
  bool _joined = false;

  // Display states
  uint8_t _page = 0;
  uint8_t _field = 0;
  bool _buttonPage = false;
  bool _buttonField = false;
  bool _buttonChange = false;
  bool _redisplayRequested = false; // Toggle this to trigger redisplay.


  public:
  AppState() : _gpsSample() {
    reset();
  }

  AppState(const AppState &otherState)
  : RespireState(otherState),
    _usbPower(otherState._usbPower),
    _batteryVolts(otherState._batteryVolts),
    _gpsFix(otherState._gpsFix),
    _gpsSample(otherState._gpsSample),
    _gpsSampleExpiry(otherState._gpsSampleExpiry),
    _ttnFrameCounter(otherState._ttnFrameCounter),
    _ttnLastSend(otherState._ttnLastSend),
    _joined(otherState._joined),
    _page(otherState._page),
    _field(otherState._field),
    _buttonPage(otherState._buttonPage),
    _buttonField(otherState._buttonField),
    _buttonChange(otherState._buttonChange),
    _redisplayRequested(otherState._redisplayRequested)
  {}

  virtual void updateDerivedState(const AppState &oldState) {
    static const uint8_t kPageCount = 3;
    if (ModeDisplay.attached() && ModeDisplay.isActive(*this)) { // Buttons change page/field only while display is on
      if (buttonPage() && !oldState.buttonPage()) {
        _page = (_page + 1) % kPageCount;
        _field = 0;
      }
      if (buttonField() && !oldState.buttonField()) {
        _field = (_field + 1) % fieldCountForPage(*this, _page);
      }
    }
  }

  void reset() {
    RespireState<AppState>::reset();
    _usbPower = false;
    _gpsFix = false;
    _joined = false;
    _gpsSampleExpiry = 0;
  }

  // USB power
  bool getUsbPower() const {
    return _usbPower;
  }

  void setUsbPower(bool value) {
    if (_usbPower == value) {
      // Short circuit no change
      return;
    }
    AppState oldState(*this);
    _usbPower = value;
    onUpdate(oldState);
  }

  float batteryVolts() const {
    return _batteryVolts;
  }

  void batteryVolts(float value) {
    if (FLOAT_SAME(_batteryVolts, value, 0.01)) {
      // Short circuit no change
      return;
    }
    AppState oldState(*this);
    _batteryVolts = value;
    onUpdate(oldState);
  }

  bool hasGpsFix() const {
    return _gpsFix;
  }

  void setGpsFix(bool value) {
    if (_gpsFix == value) {
      // Short circuit no change
      return;
    }
    AppState oldState(*this);
    _gpsFix = value;
    onUpdate(oldState);
  }

  void setGpsLocation(GpsSample gpsSample) {
    Log.Debug("setGpsLocation -----------------------------\n");
    AppState oldState(*this);
    _gpsSample = gpsSample;
    _gpsSampleExpiry = millis() + SAMPLE_VALID_FOR_MS;
    onUpdate(oldState);
  }

  const GpsSample &gpsSample() const {
    return _gpsSample;
  }

  bool hasRecentGpsLocation() const {
    return _gpsSampleExpiry != 0 && (millis() < _gpsSampleExpiry);
  }

  bool getJoined() const {
    return _joined;
  }

  void setJoined(bool value) {
    if (_joined == value) {
      // Short circuit no change
      return;
    }
    AppState oldState(*this);
    _joined = value;
    onUpdate(oldState);
  }

  bool getGpsPower() const {
    return getUsbPower() || (ModeLowPowerGpsSearch.attached() && ModeLowPowerGpsSearch.isActive(*this));
  }

  uint32_t ttnFrameCounter() const {
    return _ttnFrameCounter;
  }

  uint8_t page() const {
    return _page;
  }

  void page(uint8_t page) {
    if (_page == page) {
      // Short circuit no change
      return;
    }
    AppState oldState(*this);
    _page = page;
    onUpdate(oldState);
  }

  uint8_t field() const {
    return _field;
  }

  void field(uint8_t field) {
    if (_field == field) {
      // Short circuit no change
      return;
    }
    AppState oldState(*this);
    _field = field;
    onUpdate(oldState);
  }

  bool buttonAny() const {
    return _buttonPage || _buttonField || _buttonChange;
  }

  bool buttonPage() const {
    return _buttonPage;
  }

  void buttonPage(bool btn) {
    if (_buttonPage == btn) {
      // Short circuit no change
      return;
    }
    AppState oldState(*this);
    _buttonPage = btn;
    onUpdate(oldState);
  }

  bool buttonField() const {
    return _buttonField;
  }

  void buttonField(bool btn) {
    if (_buttonField == btn) {
      // Short circuit no change
      return;
    }
    AppState oldState(*this);
    _buttonField = btn;
    onUpdate(oldState);
  }

  bool buttonChange() const {
    return _buttonChange;
  }

  void buttonChange(bool btn) {
    if (_buttonChange == btn) {
      // Short circuit no change
      return;
    }
    AppState oldState(*this);
    _buttonChange = btn;
    onUpdate(oldState);
  }

  bool redisplayRequested() const {
    return _redisplayRequested;
  }

  void requestRedisplay() {
    // A request is represented by a change in this value. The actual value doesn't matter.
    AppState oldState(*this);
    _redisplayRequested = !_redisplayRequested;
    onUpdate(oldState);
  }

  void transmittedFrame(const uint32_t frameCounter) {
    AppState oldState(*this);
    _ttnFrameCounter = frameCounter;
    _ttnLastSend = millis();
    onUpdate(oldState);
  }

  void dump(const Mode<AppState> &mainMode = ModeMain) const {
    Log.Debug("AppState: ----------------\n");
    Log.Debug("- Millis:             %u\n", (long unsigned)millis());
    Log.Debug("- Counter:            %u\n", (long unsigned)changeCounter());
    Log.Debug("- Display Change:  %T\n", redisplayRequested());
    Log.Debug("- USB Power [Input]:  %T\n", getUsbPower());
    Log.Debug("- Battery Power [Input]:  %fV\n", batteryVolts());
    Log.Debug("- Joined [Input]:     %T\n", getJoined());
    Log.Debug("- GPS Power [Output]: %T\n", getGpsPower());
    Log.Debug("- GPS Fix [Input]:     %T\n", hasGpsFix());
    Log.Debug("- GPS Location [Input]: %T\n", hasRecentGpsLocation());
    Log.Debug("- GPS Expiry [Input]: %u\n", _gpsSampleExpiry);
    Log.Debug("- TTN Frame Up [Input]: %u\n", _ttnFrameCounter);
    Log.Debug("- TTN Last Send [Input]: %u\n", _ttnLastSend);
    Log.Debug("- Max Sleep [Calculated]: %u (where %u is a day)\n", mainMode.maxSleep(*this, DAYS_IN_MILLIS(1)), DAYS_IN_MILLIS(1));
    _gpsSample.dump();
    mainMode.dump(*this);
    Log.Debug("AppState: ---------------- END\n");
  }

  virtual void onChange(const AppState &oldState, Executor<AppState> *executor) {
    // This should be simple listener or output transducer
    if (getGpsPower()!=oldState.getGpsPower()) {
      executor->exec(changeGpsPower, *this, oldState, NULL);
    }
  }

  virtual void didUpdate(const AppState &oldState, const Mode<AppState> &mainMode, const uint16_t holdLevel) {
    dump(mainMode);
  }
};

#endif
