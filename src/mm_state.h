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
#include <cstdio>
#include <cassert>

#define ELEMENTS(_array) (sizeof(_array) / sizeof(_array[0]))
#define GPS_POWER_PIN 50

class AppState;
class Mode;

typedef void (*ListenerFn)(const AppState &state, const AppState &oldState);
typedef bool (*StateModFn)(const AppState &state, const AppState &oldState);

extern void changeGpsPower(const AppState &state, const AppState &oldState);
extern void attemptJoin(const AppState &state, const AppState &oldState);
extern void changeSleep(const AppState &state, const AppState &oldState);
extern void sendLocation(const AppState &state, const AppState &oldState);

#define MINUTES_IN_MILLIS(x) ((x) * 60 * 1000)

typedef enum TimeUnit {
  TimeUnitNone,
  TimeUnitHour,
  TimeUnitDay
} TimeUnit;

class Clock {
  public:
  virtual unsigned long millis() {
    return ::millis();
  }
};
extern Clock gClock;

class Executor {
  public:
  virtual void exec(ListenerFn listener, const AppState &state, const AppState &oldState, Mode *trigger);
};
extern Executor gExecutor;

typedef struct ModeStateTag {
  // Mode is active as of a particular history index
  uint32_t _startIndex = 0;
  uint32_t _startMillis = 0;
  uint32_t _endMillis = 0;
  uint8_t _invocationCount = 0;

  uint32_t _lastTriggerMillis = 0;

  Mode *_enclosing = NULL; // When this mode closes, enclosing one also becomes inactive. Should decouple with onDeactivate hook.
} ModeState;

class Mode {
  uint8_t _stateIndex;

  const char * const _name;
  const uint8_t _repeatLimit;
  const uint32_t _maxDuration;
  const uint16_t _perTimes;
  const TimeUnit _perUnit;

  StateModFn _inspirationFn;

  public:
  Mode(const char *name, uint8_t repeatLimit, uint32_t maxDuration = 0);

  /** Construct periodic Mode. */
  Mode(const char *name, uint16_t times, TimeUnit perUnit);

  void attach(AppState &state);

  Mode &setInspiration(StateModFn fn) {
    _inspirationFn = fn;
    return *this;
  }

  ModeState &modeState(AppState &state);
  const ModeState &modeState(const AppState &state) const;

  void reset(AppState &state) {
    // Enable mode to be used again.
    modeState(state)._invocationCount = 0;
    // TODO: Also de-activate current operation?
    // _startIndex = 0;
    // _startMillis = 0;
  }

  void setEnclosing(AppState &state, Mode *enclosing) {
    modeState(state)._enclosing = enclosing;
  }

  const uint32_t getStartIndex(const AppState &state) const {
    return modeState(state)._startIndex;
  }

  bool isActive(const AppState &state) const {
    return modeState(state)._startIndex!=0;
  }

  bool getTerminated(const AppState &state) const {
    // Cannot be used again.
    return _repeatLimit!=0 && _repeatLimit==modeState(state)._invocationCount;
  }

  /**
   * Activate if not already active and if invocationCount has not exceeded repeatLimit
   */
  bool activate(AppState &state, uint32_t currentIndex, uint32_t millis, Mode *by) {
    if (isActive(state)) {
      // Already active. Don't change anything.
      return false;
    }
    if (_repeatLimit!=0 && modeState(state)._invocationCount>=_repeatLimit) {
      // Hit repeat limit. Don't activate.
      // printf("Repeat limit\n");
      return false;
    }
    modeState(state)._startIndex = currentIndex;
    modeState(state)._startMillis = millis;
    modeState(state)._invocationCount++;
    setEnclosing(state, by);
    if (by!=NULL) {
      // by->childActive();
    }
    return true;
  }

  bool setInactive(AppState &state, uint32_t millis) {
    if (!isActive(state)) {
      // Already inactive. Don't change anything.
      // printf("Not active\n");
      return false;
    }
    modeState(state)._startIndex = 0; // Inactive
    modeState(state)._endMillis = millis;

    if (modeState(state)._enclosing!=NULL) {
      // _enclosing->childInactive(millis);
      modeState(state)._enclosing->setInactive(state, millis); // TODO: No, because it may have other children keeping it alive.
    }
    return true;
  }

  bool setInactive(AppState &state, uint32_t invocationIndex, uint32_t millis) {
    if (modeState(state)._startIndex!=invocationIndex) {
      // Not talking about the same invocation.
      // printf("Different index: %u != %u\n", _startIndex, invocationIndex);
      return false;
    }
    return setInactive(state, millis);
  }

  bool terminate(AppState &state, uint32_t millis) {
    return setInactive(state, modeState(state)._startIndex, millis);
  }

  bool expired(AppState &state, uint32_t currentMillis) {
    if (!isActive(state)) {
      // Not active. Now way to expire.
      return false;
    }
    if (_maxDuration==0) {
      // No max.
      return false;
    }
    bool expired = (modeState(state)._startMillis + _maxDuration) <= currentMillis;
    if (expired) {
      this->terminate(state, currentMillis);
    }
    return expired;
  }

  bool triggered(AppState &state, uint32_t currentMillis) {
    if (!isActive(state)) {
      // Not active. Now way to trigger.
      return false;
    }
    if (_perUnit==TimeUnitNone) {
      // No period.
      return false;
    }
    uint32_t period = 0;
    switch (_perUnit) {
      case TimeUnitDay:
        period = 24 * 60 * 60 * 1000 / _perTimes;
        break;
      case TimeUnitHour:
        period = 60 * 60 * 1000 / _perTimes;
        break;
      case TimeUnitNone:
        return false; // No period behavior
    }
    // printf("Triggered values: last=%lu, period=%lu, current=%lu\n", _lastTriggerMillis, period, currentMillis);
    bool triggered = (modeState(state)._lastTriggerMillis==0) || (modeState(state)._lastTriggerMillis + period) <= currentMillis;
    if (triggered) {
      modeState(state)._lastTriggerMillis = currentMillis;
    }
    return triggered;
  }

  void dump(AppState &state) {
    printf("Mode: \"%12s\" [%s] lastTrigger: %u\n", _name, (isActive(state) ? "Active" : "Inactive"), modeState(state)._lastTriggerMillis);
  }
};

// Modes
extern Mode ModeMain;
extern Mode ModeSleep;
extern Mode ModeAttemptJoin;
extern Mode ModeLowPowerJoin;
extern Mode ModeLowPowerGpsSearch;
extern Mode ModePeriodicSend;
extern Mode ModeSend;

class AppState {
  Clock *_clock;
  Executor *_executor;
  uint32_t _changeCounter;

  // External state
  bool _usbPower;
  bool _gpsFix;

  // Dependent state - no setters
  bool _joined;
  bool _gpsPower;

  uint8_t _modesCount = 0;
  ModeState _modeStates[10];

  public:
  AppState() : AppState(&gClock, &gExecutor)
  {
  }

  AppState(Clock *clock, Executor *executor)
  : _changeCounter(0),
    _clock(clock), _executor(executor),
    _usbPower(false),
    _gpsPower(false), _joined(false)
  {
    // ModeLowPowerJoin.setInspiration([] (AppState state, AppState oldState) { return true;})
  }

  void init();

  uint8_t allocateMode() {
    uint8_t alloc = _modesCount++;
    assert(alloc<ELEMENTS(_modeStates));
    return alloc;
  }

  ModeState &modeState(const uint8_t stateIndex) {
    return _modeStates[stateIndex];
  }

  const ModeState &modeState(const uint8_t stateIndex) const {
    return _modeStates[stateIndex];
  }

  void setExecutor(Executor *executor) {
    _executor = executor;
  }

  void loop() {
    // Check for mode timeout.
    onTime();

    // Process periodic triggers.
  }

  void complete(Mode *mode, uint32_t seqStamp) {
    // printf("----------- completing: %p %p %d\n", mode, _clock, _clock->millis());
    AppState oldState(*this);
    if (mode->setInactive(*this, seqStamp, _clock->millis())) {
      setDependent(oldState);
      onChange(oldState);
    }
  }

  void complete(Mode &mode) {
    // printf("----------- completing: %p %p %d\n", mode, _clock, _clock->millis());
    AppState oldState(*this);
    if (mode.setInactive(*this, mode.getStartIndex(*this), _clock->millis())) {
      setDependent(oldState);
      onChange(oldState);
    }
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
    setDependent(oldState);
    onChange(oldState);
  }

  bool getGpsFix() const {
    return _gpsFix;
  }

  void setGpsFix(bool value) {
    if (_gpsFix==value) {
      // Short circuit no change
      return;
    }
    AppState oldState(*this);
    _gpsFix = value;
    setDependent(oldState);
    onChange(oldState);
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
    setDependent(oldState);
    onChange(oldState);
  }

  bool getGpsPower() const {
    return _gpsPower;
  }

  Mode &getModeLowPowerJoin() {
    return ModeLowPowerJoin;
  }

  Mode &getModeAttemptJoin() {
    return ModeAttemptJoin;
  }

  Mode &getModeLowPowerGpsSearch() {
    return ModeLowPowerGpsSearch;
  }

  Mode &getModeSleep() {
    return ModeSleep;
  }

  Mode &getModePeriodicSend() {
    return ModePeriodicSend;
  }

  Mode &getModeSend() {
    return ModeSend;
  }

  void dump() {
    printf("State:\n");
    printf("Millis: %lu\n", _clock->millis());
    printf("Counter: %d\n", _changeCounter);
    printf("USB Power: %d\n", _usbPower);
    printf("Joined: %d\n", _joined);
    printf("GPS Power: %d\n", _gpsPower);
    ModeLowPowerJoin.dump(*this);
    ModeSleep.dump(*this);
    ModeAttemptJoin.dump(*this);
    ModeLowPowerGpsSearch.dump(*this);
    ModePeriodicSend.dump(*this);
    ModeSend.dump(*this);
  }

  private:
  void setDependent(const AppState &oldState) {
    #define FIRST_PASS (!ModeMain.isActive(oldState))
    #define NEWLY_TRUE(_field) ((_field) && !(oldState._field))
    #define NEWLY_FALSE(_field) (!(_field) && ((oldState._field) || FIRST_PASS))

    _changeCounter++;
    uint32_t millis = _clock->millis();

    // _joined = false; // isSet(deviceAddr) && isSet(appSessionKey) && isSet(networkSessionKey)
    _gpsPower = _usbPower || ModeLowPowerGpsSearch.isActive(*this);

    // State based activation: Some world state exists, activate mode.
    // LowPowerJoin required condition
    if (NEWLY_FALSE(_usbPower) && NEWLY_FALSE(_joined)) {
      ModeLowPowerJoin.activate(*this, _changeCounter, millis, &ModeMain);
    }
    else {
      ModeLowPowerJoin.terminate(*this, millis);
    }

    // PeriodicSend required condition
    if (_usbPower && _joined) {
      ModePeriodicSend.activate(*this, _changeCounter, millis, &ModeMain);
    }
    else {
      ModePeriodicSend.terminate(*this, millis);
    }

    // Containment activation: When LowPowerJoin becomes active, activate AttemptJoin.
    if (ModeLowPowerJoin.isActive(*this) && !ModeLowPowerJoin.isActive(oldState)) {
      ModeAttemptJoin.activate(*this, _changeCounter, millis, &ModeLowPowerJoin);
    }

    // Low power and joined, turn on GPS
    if (!_usbPower && _joined) {
      ModeLowPowerGpsSearch.activate(*this, _changeCounter, millis, &ModeMain);
    }
    else {
      ModeLowPowerGpsSearch.terminate(*this, millis);
    }
    // LowPowerGpsSearch terminating condition...
    if (ModeLowPowerGpsSearch.isActive(*this)) {
      if (_gpsFix) {
        ModeLowPowerGpsSearch.terminate(*this, millis);
      }
    }

    // Default activation: No other mode is active, activate default.
    if (!ModeAttemptJoin.isActive(*this) && !ModeLowPowerJoin.isActive(*this) && !ModeLowPowerGpsSearch.isActive(*this)) {
      ModeSleep.activate(*this, _changeCounter, millis, &ModeMain);
    }
    // ModeContinuousJoin.activate(_usbPower && !_joined);
    // dump();
  }

  void onTime() {
    bool changed = false;
    uint32_t millis = _clock->millis();
    AppState oldState(*this);
    if (ModeLowPowerGpsSearch.isActive(*this)) {
      changed |= ModeLowPowerGpsSearch.expired(*this, millis);
    }
    if (ModePeriodicSend.isActive(*this)) {
      if (ModePeriodicSend.triggered(*this, millis)) {
        ModeSend.activate(*this, _changeCounter, millis, &ModePeriodicSend);
        changed |= true;
      }
    }
    if (changed) {
      setDependent(oldState);
      onChange(oldState);
    }
  }

  void onChange(const AppState &oldState) {
    if (_gpsPower!=oldState._gpsPower) {
      _executor->exec(changeGpsPower, *this, oldState, NULL);
    }
    if (ModeAttemptJoin.isActive(*this) && !ModeAttemptJoin.isActive(oldState)) {
      // Mode is active as long as attemptJoin is running.
      _executor->exec(attemptJoin, *this, oldState, &ModeAttemptJoin);
    }
    if (ModeSleep.isActive(*this) && !ModeSleep.isActive(oldState)) {
      _executor->exec(changeSleep, *this, oldState, &ModeSleep);
    }
    if (ModeSend.isActive(*this) && !ModeSend.isActive(oldState)) {
      _executor->exec(sendLocation, *this, oldState, &ModeSend);
    }
  }
};
