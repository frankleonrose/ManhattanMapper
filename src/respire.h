#include <Arduino.h>
#include <limits.h>
#include <cstdio>
#include <cassert>
#undef min
#undef max
#include <vector>
#include <algorithm>
#include <Logging.h>
#include <functional>

#define ELEMENTS(_array) (sizeof(_array) / sizeof(_array[0]))

class AppState;
class Mode;
class RespireStateBase;

typedef void (*ListenerFn)(const AppState &state, const AppState &oldState);
typedef bool (*StateModFn)(const AppState &state, const AppState &oldState);
typedef bool (*StatePredicate)(const AppState &state);

#define MINUTES_IN_MILLIS(x) ((x) * 60 * 1000)
#define HOURS_IN_MILLIS(x) ((x) * 60 * MINUTES_IN_MILLIS(1))
#define DAYS_IN_MILLIS(x) ((x) * 24 * HOURS_IN_MILLIS(1))

typedef enum TimeUnit {
  TimeUnitNone,
  TimeUnitHour,
  TimeUnitDay
} TimeUnit;

typedef enum ActivationTag {
  ActivationInspiring,
  ActivationActive,
  ActivationSustaining, // Used by Periodic cells that aren't inspiring but neither are they removing support
  ActivationExpiring,
  ActivationInactive,
  ActivationDefaultCell // Used to tell a child it is being activated as a default cell
} ActivationType;

class Clock {
  public:
  virtual unsigned long millis() {
    return ::millis();
  }
};

class Executor {
  public:
  virtual void exec(ListenerFn listener, const AppState &state, const AppState &oldState, Mode *trigger);
};

typedef struct ModeStateTag {
  // Mode is active as of a particular history index
  uint32_t _startIndex = 0;
  uint32_t _startMillis = 0;
  uint32_t _endMillis = 0;

  bool _invocationActive = false;
  uint8_t _invocationCount = 0;

  uint32_t _lastTriggerMillis = 0;

  uint8_t _childInspirationCount = 0;
} ModeState;

#define STATE_INDEX_INITIAL 255

class Mode {
  uint8_t _stateIndex = STATE_INDEX_INITIAL;

  const char * const _name;
  const uint8_t _repeatLimit = 0;

  const uint32_t _minDuration = 0;
  const uint32_t _maxDuration = 0;
  uint32_t _minGapDuration = 0;

  const uint16_t _perTimes = 0;
  const TimeUnit _perUnit = TimeUnitNone;

  Mode *_defaultMode = NULL;
  std::vector<Mode*> _children;
  uint8_t _childActivationLimit = 0;
  uint8_t _childSimultaneousLimit = 0;

  StateModFn _inspirationFn;
  ListenerFn _invokeFunction;
  StatePredicate _requiredFunction;

  uint8_t _countParents = 0;
  uint8_t _supportiveParents = 0;
  uint32_t _supportiveFrame = 0; // changeCounter value corresponding to current supportiveParents value. Alternative is to initialize _supportiveParents = _countParents before propagation.

  void reset() {
    _supportiveFrame = 0; // Reinitialize this count
    _stateIndex = STATE_INDEX_INITIAL;
  }

  public:
  Mode(const char *name, uint8_t repeatLimit = 0, uint32_t minDuration = 0, uint32_t maxDuration = 0);

  /** Construct periodic Mode. */
  Mode(const char *name, uint16_t times, TimeUnit perUnit);

  /** Construct invoker Mode. */
  Mode(const char *name, ListenerFn invokeFunction)
  : _name(name),
    _invokeFunction(invokeFunction)
  {
  }

  void attach(RespireStateBase &state);

  void collect(std::vector<Mode*> &invokeModes, std::vector<Mode*> &timeDependentModes) {
    reset();

    if (_invokeFunction!=NULL) {
      // Don't add duplicate modes. Can't use set<> because we need reliable order of execution (addChild order)
      if (std::find(invokeModes.begin(), invokeModes.end(), this) == invokeModes.end()) {
        invokeModes.push_back(this);
      }
    }
    if (_perUnit!=TimeUnitNone || _maxDuration!=0) {
      if (std::find(timeDependentModes.begin(), timeDependentModes.end(), this) == timeDependentModes.end()) {
        timeDependentModes.push_back(this);
      }
    }
    for (auto m = _children.begin(); m!=_children.end(); ++m) {
      (*m)->collect(invokeModes, timeDependentModes);
    }
  }

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

  Mode &minGapDuration(uint32_t gap) {
    _minGapDuration = gap;
    return *this;
  }
  Mode &defaultMode(Mode *mode) {
    _defaultMode = mode;
    return *this;
  }

  Mode &addChild(Mode *child) {
    _children.push_back(child);
    ++child->_countParents;
    return *this;
  }

  Mode &childActivationLimit(uint8_t limit) {
    _childActivationLimit = limit;
    return *this;
  }

  Mode &childSimultaneousLimit(uint8_t limit) {
    _childSimultaneousLimit = limit;
    return *this;
  }

  ModeState &modeState(AppState &state);
  const ModeState &modeState(const AppState &state) const;

  bool propagate(const ActivationType parentActivation, AppState &state, const AppState &oldState);
  bool propagateActive(const ActivationType parentActivation, const ActivationType myActivation, AppState &state, const AppState &oldState);

  void reset(AppState &state) {
    // Enable mode to be used again.
    modeState(state)._invocationCount = 0;
  }

  bool isActive(const AppState &state) const {
    return modeState(state)._startIndex!=0;
  }

  bool hitRepeatLimit(const AppState &state) const {
    // Cannot be used again.
    return _repeatLimit!=0 && _repeatLimit<=modeState(state)._invocationCount;
  }

  bool invocationTerminated(const AppState &state, const AppState &oldState) const {
    return _invokeFunction!=NULL && !modeState(state)._invocationActive && modeState(oldState)._invocationActive;
  }

  uint8_t decSupportiveParents(const AppState &state);

  bool insufficientGap(const AppState &state) const;

  /**
   * Activate if not already active and if invocationCount has not exceeded repeatLimit
   */
  bool activate(AppState &state);

  bool terminate(AppState &state);

  bool expired(const AppState &state) const;

  bool triggered(const AppState &state) const;

  void dump(const AppState &state) const {
    Log.Debug("Mode: \"%20s\" [%8s][%7s]", _name,
      (isActive(state) ? "Active" : "Inactive"),
      (requiredState(state) ? "Ready" : "Unready"));
    if (_repeatLimit==0) {
      Log.Debug_(" invocations: %d,", (int)modeState(state)._invocationCount);
    }
    else {
      Log.Debug_(" invocations: %d of [%d],", (int)modeState(state)._invocationCount, (int)_repeatLimit);
    }
    if (_childSimultaneousLimit!=0) Log.Debug_(" childSimultaneous: %d,", (int)_childSimultaneousLimit);
    if (_children.size()>0) Log.Debug_(" childInspirations: %d [limit %d],", (int)modeState(state)._childInspirationCount, (int)_childActivationLimit);
    if (_perUnit != TimeUnitNone) Log.Debug_(" lastTrigger: %lu,", (long unsigned int)modeState(state)._lastTriggerMillis);
    if (_invokeFunction!=NULL) Log.Debug_(" [%11s],", modeState(state)._invocationActive ? "Running" : "Not running");
    Log.Debug_("\n");
  }
};

class RespireStateBase {
  uint32_t _changeCounter = 1;
  uint32_t _millis = 0;

  uint8_t _modesCount = 0;
  ModeState _modeStates[15];

  public:

  void reset() {
    _changeCounter = 1;
    _millis = 0;
    _modesCount = 0;
  }

  virtual void dump() const = 0;

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

  void newFrame(const uint32_t millis) {
    _changeCounter++;
    _millis = millis;
  }

  void millis(const uint32_t millis) {
    _millis = millis;
  }

  uint32_t changeCounter() const {
    return _changeCounter;
  }

  uint32_t millis() const {
    return _millis;
  }
};

template <class TAppState> class RespireState : public RespireStateBase {
  //void (*_listener)(const TAppState &oldState);
  std::function< void(const TAppState) > _listener;

  public:

  // void setListener(void (*listener)(const TAppState &oldState)) {
  //   _listener = listener;
  // }
  void setListener(const std::function< void(const TAppState) > &listener) {
    _listener = listener;
  }

  void setDependent(const AppState &oldState) {
    _listener(oldState);
  }
  virtual void onChange(const AppState &oldState, Executor *executor) = 0;
};

template <class TAppState> class RespireContext;

template <class TAppState> class StateTransaction {
  RespireContext<TAppState> &_context; // Reference to mutable state so we have end result in dtor
  const TAppState _initialState; // Copy of initial world state to compare against
  public:
  StateTransaction(RespireContext<TAppState> &context);
  ~StateTransaction();
};

template <class TAppState>
class RespireContext {
  TAppState &_appState;
  Mode &_modeMain;

  Clock *_clock;
  Executor *_executor;
  bool _initialized = false;
  uint16_t _holdLevel = 0;

  std::vector<Mode*> _invokeModes;
  std::vector<Mode*> _timeDependentModes;

  public:

  RespireContext(TAppState &appState, Mode &modeMain, Clock *clock, Executor *executor)
  : _appState(appState),
    _modeMain(modeMain),
    _clock(clock),
    _executor(executor),
    _holdLevel(1)
  {
    _appState.setListener([this](const AppState &oldState) {
      setDependent(oldState);
    });
  }

  ~RespireContext() {
    _appState.setListener(NULL);
  }

  void setExecutor(Executor *executor) {
    _executor = executor;
  }

  const TAppState &appState() const {
    return _appState;
  }

  void init() {
    Log.Debug("RespireContext::init()" CR);

    _appState.reset();

    _modeMain.collect(_invokeModes, _timeDependentModes);
    Log.Debug("RespireContext::init() %d %d" CR, _invokeModes.size(), _timeDependentModes.size());
    // assert(_timeDependentModes.size()==3);

    _modeMain.attach(_appState);

    // Main is always active
    _modeMain.activate(_appState);
    // ModeMain.dump(_appState);

    TAppState reference; // Initial rev to compare to
    // reference.dump();
    setDependent(reference);

    _initialized = true;
  }

  void begin() {
    if (!_initialized) {
      init();
    }
    TAppState reference;
    resumeActions(reference);
  }

  void complete(Mode &mode, void (*updateFn)(TAppState &state) = NULL) {
    // Log.Debug("----------- completed: %s [%s]\n", mode.name(), (mode.modeState(*this)._invocationActive ? "Running" : "Not running"));
    if (!mode.modeState(_appState)._invocationActive) {
      return;
    }
    StateTransaction<TAppState> t(*this);
    if (updateFn!=NULL) {
      updateFn(_appState);
    }
    TAppState oldState(_appState);
    mode.modeState(_appState)._invocationActive = false;
    setDependent(oldState);
  }

  void cancel(Mode &mode) {
    // Log.Debug("----------- cancelled: %s [%s]\n", mode.name(), (mode.modeState(*this)._invocationActive ? "Running" : "Not running"));
    if (!mode.modeState(_appState)._invocationActive) {
      return;
    }
    TAppState oldState(_appState);

    mode.modeState(_appState)._invocationActive = false;

    setDependent(oldState);
  }

  void holdActions() {
    ++_holdLevel;
  }

  void resumeActions(const TAppState &oldState) {
    assert(0 < _holdLevel);
    --_holdLevel;
    if (_holdLevel==0) {
      onChange(oldState);
    }
  }

  bool checkTimeTriggers() const {
    bool changed = false;

    for (auto m = _timeDependentModes.begin(); m!=_timeDependentModes.end(); ++m) {
      // Log.Debug("%s expired=%T or triggered=%T\n", (*m)->name(), (*m)->expired(_appState), (*m)->triggered(_appState));
      changed |= (*m)->expired(_appState);
      changed |= (*m)->triggered(_appState);
    }

    return changed;
  }

  void loop() {
    // Update the time so that periodic checks work.
    _appState.millis(_clock->millis());

    if (checkTimeTriggers()) {
      TAppState oldState(_appState);
      setDependent(oldState); // TODO: Just pass _appState. It's a const arg.
    }
  }

  void setDependent(const AppState &oldState) {
    _appState.newFrame(_clock->millis());

    // _joined = false; // isSet(deviceAddr) && isSet(appSessionKey) && isSet(networkSessionKey)

    _modeMain.propagate(ActivationActive, _appState, oldState);

    if (_holdLevel==0) {
      onChange(oldState);
    }

    _appState.dump();
  }

  void onChange(const TAppState &oldState) {
    _appState.onChange(oldState, _executor);

    for (auto m = _invokeModes.begin(); m!=_invokeModes.end(); ++m) {
      auto mode = *m;
      if (mode->isActive(_appState) && !mode->isActive(oldState)) {
        // printf("Invoke: %s %p\n", mode->name(), mode->invokeFunction());
        _executor->exec(mode->invokeFunction(), _appState, oldState, mode);
      }
    }
  }
};

template <class TAppState>
StateTransaction<TAppState>::StateTransaction(RespireContext<TAppState> &context)
: _context(context), _initialState(context.appState())
{
  _context.holdActions();
}

template <class TAppState>
StateTransaction<TAppState>::~StateTransaction() {
  _context.resumeActions(_initialState);
}
