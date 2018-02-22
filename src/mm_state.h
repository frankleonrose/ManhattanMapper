#include <Arduino.h>
#include <cstdio>

class AppState;
class Mode;

typedef void (*ListenerFn)(const AppState &state, const AppState &oldState);

extern void changeGpsPower(const AppState &state, const AppState &oldState);
extern void attemptJoin(const AppState &state, const AppState &oldState);
extern void changeSleep(const AppState &state, const AppState &oldState);

#define MINUTES_IN_MILLIS(x) ((x) * 60 * 1000)

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

class Mode {
  // Mode is active as of a particular history index
  const char * const _name;
  uint32_t _startIndex;
  uint32_t _startMillis;
  uint32_t _endMillis;
  uint8_t _repeatLimit;
  uint32_t _maxDuration;
  uint8_t _invocationCount;
  Mode *_enclosing; // When this mode closes, enclosing one also becomes inactive. Should decouple with onDeactivate hook.

  public:
  Mode(const char *name, uint8_t repeatLimit, uint32_t maxDuration = 0)
  : _name(name), _startIndex(0), _startMillis(0), _repeatLimit(repeatLimit), _maxDuration(maxDuration), _invocationCount(0), _enclosing(NULL)
  {
  }

  void reset() {
    // Enable mode to be used again.
    _invocationCount = 0;
    // TODO: Also de-activate current operation?
    // _startIndex = 0;
    // _startMillis = 0;
  }

  void setEnclosing(Mode *enclosing) {
    _enclosing = enclosing;
  }

  const uint32_t getStartIndex() const {
    return _startIndex;
  }

  bool isActive() const {
    return _startIndex!=0;
  }

  bool getTerminated() const {
    // Cannot be used again.
    return _repeatLimit!=0 && _repeatLimit==_invocationCount;
  }

  /**
   * Activate if not already active and if invocationCount has not exceeded repeatLimit
   */
  bool activate(uint32_t currentIndex, uint32_t millis) {
    if (isActive()) {
      // Already active. Don't change anything.
      return false;
    }
    if (_repeatLimit!=0 && _invocationCount>=_repeatLimit) {
      // Hit repeat limit. Don't activate.
      // printf("Repeat limit\n");
      return false;
    }
    _startIndex = currentIndex;
    _startMillis = millis;
    _invocationCount++;
    return true;
  }

  bool setInactive(uint32_t invocationIndex, uint32_t millis) {
    if (!isActive()) {
      // Already inactive. Don't change anything.
      // printf("Not active\n");
      return false;
    }
    if (_startIndex!=invocationIndex) {
      // Not talking about the same invocation.
      // printf("Different index: %u != %u\n", _startIndex, invocationIndex);
      return false;
    }
    _startIndex = 0; // Inactive
    _endMillis = millis;

    if (_enclosing!=NULL) {
      _enclosing->setInactive(_enclosing->getStartIndex(), millis);
    }
    return true;
  }

  bool terminate(uint32_t millis) {
    setInactive(_startIndex, millis);
  }

  bool expired(uint32_t currentMillis) {
    if (!isActive()) {
      // Not active. Now way to expire.
      return false;
    }
    if (_maxDuration==0) {
      // No max.
      return false;
    }
    return (_startMillis + _maxDuration) <= currentMillis;
  }

  void dump() {
    printf("Mode: \"%12s\" [%s]\n", _name, (isActive() ? "Active" : "Inactive"));
  }
};

class AppState {
  Clock *_clock;
  Executor *_executor;
  uint32_t _changeCounter;

  // External state
  bool _usbPower;
  bool _gpsFix;

  // Modes
  Mode _modeSleep;
  Mode _modeAttemptJoin;
  Mode _modeLowPowerJoin;
  Mode _modeLowPowerGpsSearch;

  // Dependent state - no setters
  bool _joined;
  bool _gpsPower;

  public:
  AppState() : AppState(&gClock, &gExecutor)
  {
  }

  AppState(Clock *clock, Executor *executor)
  : _changeCounter(0),
    _clock(clock), _executor(executor),
    _usbPower(false),
    _gpsPower(false), _joined(false),
    _modeAttemptJoin("AttemptJoin", 0),
    _modeLowPowerJoin("LowPowerJoin", 1),
    _modeLowPowerGpsSearch("LowPowerGpsSearch", 1, MINUTES_IN_MILLIS(5)),
    _modeSleep("Sleep", 0)
  {
  }

  void init() {
    AppState reference; // Initial rev to compare to
    setDependent(reference);
    onChange(reference);
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
    if (mode->setInactive(seqStamp, _clock->millis())) {
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
    return _modeLowPowerJoin;
  }

  Mode &getModeAttemptJoin() {
    return _modeAttemptJoin;
  }

  Mode &getModeLowPowerGpsSearch() {
    return _modeLowPowerGpsSearch;
  }

  Mode &getModeSleep() {
    return _modeSleep;
  }

  void dump() {
    printf("State:\n");
    printf("Millis: %d\n", _clock->millis());
    printf("Counter: %d\n", _changeCounter);
    printf("USB Power: %d\n", _usbPower);
    printf("Joined: %d\n", _joined);
    printf("GPS Power: %d\n", _gpsPower);
    _modeLowPowerJoin.dump();
    _modeSleep.dump();
    _modeAttemptJoin.dump();
    _modeLowPowerGpsSearch.dump();
  }

  private:
  void setDependent(const AppState &oldState) {
    _changeCounter++;
    uint32_t millis = _clock->millis();

    // _joined = false; // isSet(deviceAddr) && isSet(appSessionKey) && isSet(networkSessionKey)
    _gpsPower = _usbPower || _modeLowPowerGpsSearch.isActive();

    // State based activation: Some world state exists, activate mode.
    if (!_usbPower && !_joined) {
      _modeLowPowerJoin.activate(_changeCounter, millis);
    }

    // Containment activation: When LowPowerJoin becomes active, activate AttemptJoin.
    if (_modeLowPowerJoin.isActive() && !oldState._modeLowPowerJoin.isActive()) {
      if (_modeAttemptJoin.activate(_changeCounter, millis)) {
        _modeAttemptJoin.setEnclosing(&_modeLowPowerJoin);
      }
    }

    // Low power and joined, turn on GPS
    if (!_usbPower && _joined) {
      _modeLowPowerGpsSearch.activate(_changeCounter, millis);
    }
    // Terminating condition...
    if (_modeLowPowerGpsSearch.isActive()) {
      if (_gpsFix) {
        _modeLowPowerGpsSearch.terminate(millis);
      }
    }

    // Default activation: No other mode is active, activate default.
    if (!_modeAttemptJoin.isActive() && !_modeLowPowerJoin.isActive() && !_modeLowPowerGpsSearch.isActive()) {
      _modeSleep.activate(_changeCounter, millis);
    }
    // _modeContinuousJoin.activate(_usbPower && !_joined);
    // dump();
  }

  void onTime() {
    bool changed = false;
    uint32_t millis = _clock->millis();
    AppState oldState(*this);
    if (_modeLowPowerGpsSearch.isActive()) {
      if (_modeLowPowerGpsSearch.expired(millis)) {
        _modeLowPowerGpsSearch.terminate(millis);
        changed = true;
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
    if (_modeAttemptJoin.isActive() && !oldState._modeAttemptJoin.isActive()) {
      // Mode is active as long as attemptJoin is running.
      _executor->exec(attemptJoin, *this, oldState, &_modeAttemptJoin);
    }
    if (_modeSleep.isActive() && !oldState._modeSleep.isActive()) {
      _executor->exec(changeSleep, *this, oldState, &_modeSleep);
    }
  }
};
