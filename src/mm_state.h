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
#include <vector>

#define ELEMENTS(_array) (sizeof(_array) / sizeof(_array[0]))
#define GPS_POWER_PIN 50

class AppState;
class Mode;

typedef void (*ListenerFn)(const AppState &state, const AppState &oldState);
typedef bool (*StateModFn)(const AppState &state, const AppState &oldState);
typedef bool (*StatePredicate)(const AppState &state);

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

typedef enum ActivationTag {
  ActivationInspiring,
  ActivationActive,
  ActivationExpiring,
  ActivationInactive,
  ActivationDefaultCell
} ActivationType;

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

  bool _invocationActive = false;
  uint8_t _invocationCount = 0;

  uint32_t _lastTriggerMillis = 0;
} ModeState;

class Mode {
  uint8_t _stateIndex;

  const char * const _name;
  const uint8_t _repeatLimit = 0;
  const uint32_t _minDuration = 0;
  const uint32_t _maxDuration = 0;
  const uint16_t _perTimes = 0;
  const TimeUnit _perUnit = TimeUnitNone;

  Mode *_defaultMode = NULL;
  std::vector<Mode*> _children;

  StateModFn _inspirationFn;
  ListenerFn _invokeFunction;
  StatePredicate _requiredFunction;

  uint8_t _countParents;
  uint8_t _supportiveParents;
  uint32_t _supportiveFrame; // changeCounter value corresponding to current supportiveParents value. Alternative is to initialize _supportiveParents = _countParents before propagation.

  public:
  Mode(const char *name, uint8_t repeatLimit, uint32_t minDuration = 0, uint32_t maxDuration = 0);

  /** Construct periodic Mode. */
  Mode(const char *name, uint16_t times, TimeUnit perUnit);

  Mode(const char *name, ListenerFn invokeFunction)
  : _name(name),
    _invokeFunction(invokeFunction)
  {
  }

  /** Construct invoker Mode. */

  void attach(AppState &state);

  bool requiredState(const AppState &state) const {
    if (_requiredFunction==NULL) {
      return true;
    }
    return _requiredFunction(state);
  }

  Mode &requiredFunction(StatePredicate fn) {
    _requiredFunction = fn;
    return *this;
  }

  Mode &invokeFunction(ListenerFn fn) {
    _invokeFunction = fn;
    return *this;
  }

  ListenerFn invokeFunction() const {
    return _invokeFunction;
  }

  bool persistent(const AppState &state) const;

  Mode &setInspiration(StateModFn fn) {
    _inspirationFn = fn;
    return *this;
  }

  const char *name() const {
    return _name;
  }

  ActivationType activation(const AppState &state, const AppState &oldState) const;

  Mode &defaultMode(Mode *mode) {
    _defaultMode = mode;
    return *this;
  }

  Mode &addChild(Mode *child) {
    _children.push_back(child);
    ++child->_countParents;
    return *this;
  }

  ModeState &modeState(AppState &state);
  const ModeState &modeState(const AppState &state) const;

  bool propagate(const ActivationType parentActivation, AppState &state, const AppState &oldState);

  void reset(AppState &state) {
    // Enable mode to be used again.
    modeState(state)._invocationCount = 0;
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
  bool activate(AppState &state);

  bool terminate(AppState &state);

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
      this->terminate(state);
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

  void dump(const AppState &state) const {
    printf("Mode: \"%12s\" [%8s][%7s]", _name,
      (isActive(state) ? "Active" : "Inactive"),
      (requiredState(state) ? "Ready" : "Unready"));
    if (_perUnit != TimeUnitNone) printf(" lastTrigger: %u", modeState(state)._lastTriggerMillis);
    if (_invokeFunction!=NULL) printf(" [%11s]", modeState(state)._invocationActive ? "Running" : "Not running");
    printf("\n");
  }
};

// Modes
extern Mode ModeMain;
extern Mode ModeSleep;
extern Mode ModeAttemptJoin;
extern Mode ModeLowPowerJoin;
extern Mode ModeLowPowerGpsSearch;
extern Mode ModePeriodicJoin;
extern Mode ModePeriodicSend;
extern Mode ModeSend;
extern std::vector<Mode*> InvokeModes;

class AppState {
  Clock *_clock;
  Executor *_executor;
  uint16_t _holdLevel = 0;
  uint32_t _changeCounter = 1;

  // External state
  bool _usbPower;
  bool _gpsFix;

  // Dependent state - no setters
  bool _joined;
  bool _gpsPowerOut;

  uint8_t _modesCount = 0;
  ModeState _modeStates[10];

  public:
  AppState() : AppState(&gClock, &gExecutor)
  {
  }

  AppState(Clock *clock, Executor *executor)
  : _clock(clock), _executor(executor),
    _usbPower(false),
    _joined(false),
    _gpsPowerOut(false)
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

  uint32_t changeCounter() const {
    return _changeCounter;
  }

  uint32_t millis() const {
    return _clock->millis();
  }

  void loop() {
    // Check for mode timeout.
    onTime();

    // Process periodic triggers.
  }

  void complete(Mode &mode) {
    // printf("----------- completed: %s [%s]\n", mode.name(), (mode.modeState(*this)._invocationActive ? "Running" : "Not running"));
    if (!mode.modeState(*this)._invocationActive) {
      return;
    }
    mode.modeState(*this)._invocationActive = false;

    AppState oldState(*this);
    setDependent(oldState);
  }

  void cancel(Mode &mode) {
    // printf("----------- cancelled: %s [%s]\n", mode.name(), (mode.modeState(*this)._invocationActive ? "Running" : "Not running"));
    if (!mode.modeState(*this)._invocationActive) {
      return;
    }
    mode.modeState(*this)._invocationActive = false;

    AppState oldState(*this);
    setDependent(oldState);
  }

  void holdActions() {
    ++_holdLevel;
  }

  void resumeActions(const AppState &oldState) {
    assert(0 < _holdLevel);
    --_holdLevel;
    if (_holdLevel==0) {
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
  }

  bool getGpsPower() const {
    return _gpsPowerOut;
  }

  void dump() const {
    printf("State:\n");
    printf("Millis: %lu\n", _clock->millis());
    printf("Counter: %u\n", _changeCounter);
    printf("USB Power [Input]: %d\n", _usbPower);
    printf("Joined [Input]: %d\n", _joined);
    printf("GPS Power [Output]: %d\n", _gpsPowerOut);
    ModeLowPowerJoin.dump(*this);
    ModePeriodicJoin.dump(*this);
    ModeSleep.dump(*this);
    ModeAttemptJoin.dump(*this);
    ModeLowPowerGpsSearch.dump(*this);
    ModePeriodicSend.dump(*this);
    ModeSend.dump(*this);
  }

  private:
  void setDependent(const AppState &oldState) {
    _changeCounter++;

    // _joined = false; // isSet(deviceAddr) && isSet(appSessionKey) && isSet(networkSessionKey)

    ModeMain.propagate(ActivationActive, *this, oldState);

    if (_holdLevel==0) {
      onChange(oldState);
    }

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
        ModeSend.activate(*this);
        changed |= true;
      }
    }
    if (changed) {
      setDependent(oldState);
    }
  }

  void onChange(const AppState &oldState) {
    _gpsPowerOut = _usbPower || ModeLowPowerGpsSearch.isActive(*this);

    // This should be simple listener or output transducer
    if (_gpsPowerOut!=oldState._gpsPowerOut) {
      _executor->exec(changeGpsPower, *this, oldState, NULL);
    }

    for (auto m = InvokeModes.begin(); m!=InvokeModes.end(); ++m) {
      auto mode = *m;
      if (mode->isActive(*this) && !mode->isActive(oldState)) {
        _executor->exec(mode->invokeFunction(), *this, oldState, mode);
      }
    }
  }
};

class StateTransaction {
  AppState &_activeState; // Reference to mutable state so we have end result in dtor
  const AppState _initialState; // Copy of initial world state to compare against
  public:
  StateTransaction(AppState &state)
  : _activeState(state), _initialState(state)
  {
    _activeState.holdActions();
  }
  ~StateTransaction() {
    _activeState.resumeActions(_initialState);
  }
};
