#include <Arduino.h>

class AppState;

typedef void (*ListenerFn)(const AppState &state, const AppState &oldState);

extern void onChangeGpsPower(const AppState &state, const AppState &oldState);
extern void onAttemptJoin(const AppState &state, const AppState &oldState);

class Executor {
  public:
  virtual void exec(ListenerFn listener, AppState state, AppState oldState);
};

extern Executor gExecutor;

class Mode {
  // Mode is active as of a particular history index
  uint32_t _startIndex;
  uint32_t _startMillis;
  uint8_t _repeatLimit;
  uint8_t _activeCount;

  public:
  Mode(uint8_t repeatLimit)
  : _startIndex(0), _startMillis(0), _repeatLimit(repeatLimit), _activeCount(0)
  {
  }

  void reset() {
    // Enable mode to be used again.
    _activeCount = 0;
    // TODO: Also de-activate current operation?
    // _startIndex = 0;
    // _startMillis = 0;
  }

  bool getActive() const {
    return _startIndex!=0;
  }

  void setActive(uint32_t currentIndex, uint32_t millis) {
    if (getActive()) {
      // Already active. Don't change anything.
      return;
    }
    if (_repeatLimit!=0 && _activeCount>=_repeatLimit) {
      // Hit repeat limit. Don't activate.
      return;
    }
    _startIndex = currentIndex;
    _startMillis = millis;
    _activeCount++;
  }
};

class AppState {
  Executor *_executor;
  uint32_t _changeCounter;
  uint32_t _millis;

  // External state
  bool _usbPower;

  // Modes
  Mode _modeAttemptJoin;
  Mode _modeLowPowerJoin;

  // Dependent state - no setters
  bool _joined;
  bool _gpsPower;

  public:
  AppState() : AppState(&gExecutor)
  {
  }

  AppState(Executor *executor)
  : _changeCounter(0), _executor(executor),
    _usbPower(false),
    _gpsPower(false), _joined(false),
    _modeAttemptJoin(0),
    _modeLowPowerJoin(1)
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

  bool getGpsPower() const {
    return _gpsPower;
  }

  bool getJoined() const {
    return _joined;
  }

  private:
  void setDependent(const AppState &oldState) {
    _changeCounter++;

    _joined = false; // isSet(deviceAddr) && isSet(appSessionKey) && isSet(networkSessionKey)
    _gpsPower = _usbPower || (!_usbPower && _joined);

    if (!_usbPower && !_joined) {
      _modeLowPowerJoin.setActive(_changeCounter, _millis);
    }
    if (_modeLowPowerJoin.getActive() && !oldState._modeLowPowerJoin.getActive()) {
      _modeAttemptJoin.setActive(_changeCounter, _millis);
    }
    // _modeContinuousJoin.setActive(_usbPower && !_joined);
  }

  void onChange(const AppState &oldState) const {
    if (_gpsPower!=oldState._gpsPower) {
      _executor->exec(onChangeGpsPower, *this, oldState);
    }
    if (_modeAttemptJoin.getActive() && !oldState._modeAttemptJoin.getActive()) {
      _executor->exec(onAttemptJoin, *this, oldState);
    }
  }
};
