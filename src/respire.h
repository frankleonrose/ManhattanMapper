#ifndef RESPIRE_H
#define RESPIRE_H

/*
TODO
- Send Invocation memento that includes start frame so that on completion we are sure to be talking about the right invocation.
- When an invoke mode is terminated while running, it should notify the running function - maybe it can cancel.
- Defend against millis rollover (6.x weeks). When millis count exceeds a week or two, reset all stored times by a basis and henceforth report (millis - basis).
-
*/

#if !defined(RS_ASSERT)
#define RS_ASSERT(x) while (!(x)) { Log.Error("Assertion failure: " #x ); delay(1000); }
#endif
#if !defined(RS_ASSERT_MSG)
#define RS_ASSERT_MSG(x, msg) if (!(x)) { Log.Error("Assertion failure: " #x "[" msg "]"); }
#endif

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

typedef void (*ActionFn)(const AppState &state, const AppState &oldState, Mode *triggeringMode);
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
  ActivationIdleCell // Used to tell a child it is being activated as a idle cell
} ActivationType;

class Clock {
  public:
  virtual unsigned long millis() {
    return ::millis();
  }
};

/**
 * Executor gives us an opportunity to capture the execution of actions. Used for testing.
 */
class Executor {
  public:
  virtual void exec(ActionFn action, const AppState &state, const AppState &oldState, Mode *trigger);
};

/**
 * The ModeState struct holds the mutable state of a Mode.
 *
 * The idea is that there's a lot of static information about Modes. If the AppState object held all an
 * app's Modes, copying it would be more time consuming and cumbersome.
 *
 * Instead, Modes exist outside of AppState but all ModeStates exists within it.
 * ModeStates are each 20 bytes (as of 20180323) and copying can be accomplished with the equivalent of memcpy.
 *
 * Ideally we would use some sort of shared structure immutable data structure for states which would
 * make copies even cheaper. For now, we have this and on modern mcu's it isn't terrible.
 */
typedef struct ModeStateTag {
  // Mode is active as of a particular history index
  uint32_t _startIndex = 0; // 0 means Mode is inactive
  uint32_t _startMillis = 0;
  uint32_t _endMillis = 0;

  uint32_t _lastTriggerMillis = 0;

  bool _invocationActive = false;
  uint8_t _invocationCount = 0;

  uint8_t _childInspirationCount = 0;
} ModeState;

#define STATE_INDEX_INITIAL 255

class Mode {
  public:

  class Builder {
    const char * const _name;
    uint8_t _repeatLimit = 0;

    uint32_t _minDuration = 0;
    uint32_t _maxDuration = 0;
    uint32_t _minGapDuration = 0;
    uint32_t _invokeDelay = 0;

    uint16_t _perTimes = 0;
    TimeUnit _perUnit = TimeUnitNone;

    Mode *_idleMode = NULL;
    Mode *_followMode = NULL;
    std::vector<Mode*> _children;
    uint8_t _childActivationLimit = 0;
    uint8_t _childSimultaneousLimit = 0;

    StateModFn _inspirationPred = NULL;
    ActionFn _invokeFunction = NULL;
    StatePredicate _requiredPred = NULL;

    friend Mode; // Access these private members without creating accessor functions.

    public:

    Builder(const char *name)
    : _name(name) {
    }

    Builder &repeatLimit(uint8_t repeatLimit) {
      _repeatLimit = repeatLimit;
      return *this;
    }
    Builder &minDuration(uint32_t minDuration) {
      _minDuration = minDuration;
      return *this;
    }
    Builder &maxDuration(uint32_t maxDuration) {
      _maxDuration = maxDuration;
      return *this;
    }
    Builder &minGapDuration(uint32_t minGapDuration) {
      _minGapDuration = minGapDuration;
      return *this;
    }
    Builder &invokeDelay(uint32_t invokeDelay) {
      _invokeDelay = invokeDelay;
      return *this;
    }
    Builder &periodic(uint16_t times, TimeUnit perUnit) {
      return perTimes(times).perUnit(perUnit);
    }
    Builder &perTimes(uint16_t perTimes) {
      _perTimes = perTimes;
      return *this;
    }
    Builder &perUnit(TimeUnit perUnit) {
      _perUnit = perUnit;
      return *this;
    }
    Builder &idleMode(Mode *idleMode) {
      _idleMode = idleMode;
      return *this;
    }
    Builder &followMode(Mode *followMode) {
      _followMode = followMode;
      return *this;
    }
    Builder &addChild(Mode *child) {
      RS_ASSERT(child!=NULL);
      _children.push_back(child);
      return *this;
    }
    Builder &childActivationLimit(uint8_t childActivationLimit) {
      _childActivationLimit = childActivationLimit;
      return *this;
    }
    Builder &childSimultaneousLimit(uint8_t childSimultaneousLimit) {
      _childSimultaneousLimit = childSimultaneousLimit;
      return *this;
    }
    Builder &inspirationPred(StateModFn inspirationPred) {
      _inspirationPred = inspirationPred;
      return *this;
    }
    Builder &invokeFn(ActionFn invokeFunction) {
      _invokeFunction = invokeFunction;
      return *this;
    }
    Builder &requiredPred(StatePredicate requiredPred) {
      _requiredPred = requiredPred;
      return *this;
    }
  };

  private:

  uint8_t _stateIndex = STATE_INDEX_INITIAL;

  public: // All const. No worries.
  const char * const _name;
  const uint8_t _repeatLimit = 0;

  const uint32_t _minDuration = 0;
  const uint32_t _maxDuration = 0;
  const uint32_t _minGapDuration = 0;
  const uint32_t _invokeDelay = 0;

  const uint16_t _perTimes = 0;
  const TimeUnit _perUnit = TimeUnitNone;

  Mode * const _idleMode = NULL;
  Mode * const _followMode = NULL;
  const std::vector<Mode*> _children;
  const uint8_t _childActivationLimit = 0;
  const uint8_t _childSimultaneousLimit = 0;

  const StateModFn _inspirationPred = NULL;
  const ActionFn _invokeFunction = NULL;
  const StatePredicate _requiredPred = NULL;

  private:
  uint8_t _countParents = 0;
  uint8_t _supportiveParents = 0;
  uint32_t _supportiveFrame = 0; // changeCounter value corresponding to current supportiveParents value. Alternative is to initialize _supportiveParents = _countParents before propagation.

  void reset() {
    _supportiveFrame = 0; // Reinitialize this count
    _stateIndex = STATE_INDEX_INITIAL;
  }

  public:
  Mode(const Builder &builder);

  void deepReset() {
    reset();
    for (auto m = _children.begin(); m!=_children.end(); ++m) {
      (*m)->deepReset();
    }
  }

  bool attached() const { return _stateIndex != STATE_INDEX_INITIAL; }

  void attach(RespireStateBase &state);

  void collect(std::vector<Mode*> &invokeModes, std::vector<Mode*> &timeDependentModes) {
    reset();

    if (_invokeFunction!=NULL) {
      // Don't add duplicate modes. Can't use set<> because we need reliable order of execution (addChild order)
      if (std::find(invokeModes.begin(), invokeModes.end(), this) == invokeModes.end()) {
        invokeModes.push_back(this);
      }
    }
    if (_perUnit!=TimeUnitNone || _maxDuration!=0 || _minDuration || _invokeDelay) {
      if (std::find(timeDependentModes.begin(), timeDependentModes.end(), this) == timeDependentModes.end()) {
        timeDependentModes.push_back(this);
      }
    }
    for (auto m = _children.begin(); m!=_children.end(); ++m) {
      (*m)->collect(invokeModes, timeDependentModes);
    }
  }

  bool requiredState(const AppState &state) const {
    if (!_requiredPred) {
      return true;
    }
    return _requiredPred(state);
  }

  bool inspired(const AppState &state, const AppState &oldState) const {
    return (_inspirationPred && _inspirationPred(state, oldState))
          || (requiredState(state) && !requiredState(oldState));
  }

  ActionFn invokeFunction() const {
    return _invokeFunction;
  }

  uint32_t invokeDelay() const {
    return _invokeDelay;
  }

  bool persistent(const AppState &state) const;

  const char *name() const {
    return _name;
  }

  ActivationType activation(const AppState &state, const AppState &oldState) const;

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

  bool inspiring(ActivationType parentActivation, const AppState &state, const AppState &oldState) const;

  void dump(const AppState &state) const {
    Log.Debug("Mode: \"%20s\" [%8s][%7s] parents=%d", _name,
      (isActive(state) ? "Active" : "Inactive"),
      (requiredState(state) ? "Ready" : "Unready"),
      _countParents);
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
    if (_invokeDelay!=0) Log.Debug_(" invokeDelay: %d,", (int)_invokeDelay);
    if (_invokeDelay!=0 && modeState(state)._invocationActive)  Log.Debug_(" lastTrigger: %lu,", (long unsigned int)modeState(state)._lastTriggerMillis);
    Log.Debug_("\n");

    for (auto m = _children.begin(); m!=_children.end(); ++m) {
      (*m)->dump(state);
    }
  }
};

// template<int StateCount = 10>
class RespireStateBase {
  uint32_t _changeCounter = 1;
  uint32_t _millis = 0;

  uint8_t _modesCount = 0;
  ModeState _modeStates[25];

  public:

  void reset() {
    _changeCounter = 1;
    _millis = 0;
    _modesCount = 0;
  }

  virtual void dump(const Mode &mainMode) const = 0;

  uint8_t allocateMode() {
    uint8_t alloc = _modesCount++;
    RS_ASSERT(alloc<ELEMENTS(_modeStates));
    return alloc;
  }

  ModeState &modeState(const uint8_t stateIndex) {
    RS_ASSERT(stateIndex!=STATE_INDEX_INITIAL);
    return _modeStates[stateIndex];
  }

  const ModeState &modeState(const uint8_t stateIndex) const {
    RS_ASSERT(stateIndex!=STATE_INDEX_INITIAL);
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

template <class TAppState> class RespireContext;

template <class TAppState> class RespireState : public RespireStateBase {
  public:
  typedef std::function< void(const TAppState&, const TAppState&)> ListenerFn;

  private:
  RespireContext<TAppState> *_context;
  ListenerFn _listener;

  public:

  void setContext(RespireContext<TAppState> *context) {
    _context = context;
  }

  void setListener(const ListenerFn &listener) {
    _listener = listener;
  }

  void onUpdate(const TAppState &oldState) {
    // Change any derived state. Alternatively, derived state could be
    updateDerivedState(oldState);
    if (_context) {
      // Update modes held by RespireContext
      _context->onUpdate(oldState);
    }
    if (_listener) {
      // Call optional state change listener...
      _listener(*static_cast<TAppState*>(this), oldState);
    }
  }
  virtual void updateDerivedState(const TAppState &oldState) {};
  virtual void onChange(const TAppState &oldState, Executor *executor) = 0;
};

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
    _appState.setContext(this);
  }

  ~RespireContext() {
    // This dtor is most useful for tests.
    // Generally not called in app because RespireContext is global that never leaves scope.
    _appState.setContext(NULL);
    _modeMain.deepReset();
    // Walk all nodes and restore to initial values.
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

    _modeMain.attach(_appState);

    // Main is always active
    _modeMain.activate(_appState);
    // _modeMain.dump(_appState);

    TAppState reference; // Initial rev to compare to
    // reference.dump();
    onUpdate(reference);

    _initialized = true;
  }

  void begin() {
    if (!_initialized) {
      init();
    }
    TAppState reference;
    resumeActions(reference);
  }

  void complete(Mode &mode, const std::function< void(TAppState&) > &updateFn = [](TAppState&) {}) {
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
    onUpdate(oldState);
  }
  void complete(Mode *mode, const std::function< void(TAppState&) > &updateFn = [](TAppState&) {}) {
    complete(*mode, updateFn);
  }

  void holdActions() {
    ++_holdLevel;
  }

  void resumeActions(const TAppState &oldState) {
    RS_ASSERT(0 < _holdLevel);
    --_holdLevel;
    if (_holdLevel==0) {
      performActions(oldState);
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
      TAppState oldState(_appState); // TODO: Why do we need this?
      onUpdate(oldState); // The only thing that changed was millis. Don't bother making a copy of state for comparison.
    }
  }

  void onUpdate(const AppState &oldState) {
    _appState.newFrame(_clock->millis());

    _modeMain.propagate(ActivationActive, _appState, oldState);

    if (_holdLevel==0) {
      performActions(oldState);
    }

    // _appState.dump();
  }

  void performActions(const TAppState &oldState) {
    _appState.onChange(oldState, _executor);

    for (auto m = _invokeModes.begin(); m!=_invokeModes.end(); ++m) {
      auto mode = *m;
      bool invoke = false;
      if (mode->invokeDelay()==0) {
        // Just inspired. Invoke immediately.
        invoke = mode->isActive(_appState) && !mode->isActive(oldState);
      }
      else {
        // triggered() checks for delay passed and propagate sets lastTriggerMillis to current when triggered.
        invoke = _appState.millis()==mode->modeState(_appState)._lastTriggerMillis;
      }
      if (invoke) {
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

#endif