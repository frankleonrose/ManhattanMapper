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
#define RS_ASSERT(x) if (!(x)) { Log.Error("Assertion failure: " #x CR); while(true) Log.Error("."); }
#endif
#if !defined(RS_ASSERT_MSG)
#define RS_ASSERT_MSG(x, msg) if (!(x)) { Log.Error("Assertion failure: " #x "[" msg "]" CR); while(true) Log.Error("."); }
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

class RespireStateBase;

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

/**
 * Clock is a time source. Useful in testing where you might want to make time pass at different speeds.
 */
class Clock {
  protected:

  uint32_t _baseTime = 0;
  uint32_t _baseMillis = 0;

  public:

  Clock() {
    _baseMillis = ::millis();
  }
  virtual uint32_t millis() {
    return ::millis();
  }
  virtual uint32_t currentTime(const uint32_t currentTime) {
    uint32_t last = _baseTime;
    _baseTime = currentTime;
    _baseMillis = millis();
    return last;
  }
};

class RespireStore {
  public:

  virtual void beginTransaction();
  virtual void endTransaction();

  virtual bool load(const char *name, uint8_t *bytes, const uint16_t size) = 0;
  virtual bool load(const char *name, uint32_t *value) = 0;

  virtual bool store(const char *name, const uint8_t *bytes, const uint16_t size) = 0;
  virtual bool store(const char *name, const uint32_t value) = 0;
};

/**
 * The ModeState struct holds the mutable state of a Mode.
 *
 * The idea is that there's a lot of static information about Modes. If the TAppState object held all an
 * app's Modes, copying it would be more time consuming and cumbersome.
 *
 * Instead, Modes exist outside of TAppState but all ModeStates exists within it.
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

template <class TAppState>
class Mode {
  public:

  typedef void (*ActionFn)(const TAppState &state, const TAppState &oldState, Mode<TAppState> *triggeringMode);
  typedef bool (*StateModFn)(const TAppState &state, const TAppState &oldState);
  typedef bool (*StatePredicate)(const TAppState &state);


  class Builder {
    const char * const _name;
    const char *_storageTag = NULL;
    uint8_t _repeatLimit = 0;

    uint32_t _minDuration = 0;
    uint32_t _maxDuration = 0;
    uint32_t _minGapDuration = 0;
    uint32_t _invokeDelay = 0;

    uint16_t _perTimes = 0;
    TimeUnit _perUnit = TimeUnitNone;

    Mode<TAppState> *_idleMode = NULL;
    Mode<TAppState> *_followMode = NULL;
    std::vector<Mode<TAppState>*> _children;
    uint8_t _childActivationLimit = 0;
    uint8_t _childSimultaneousLimit = 0;

    StateModFn _inspirationPred = NULL;
    ActionFn _invokeFunction = NULL;
    StatePredicate _requiredPred = NULL;

    friend Mode<TAppState>; // Access these private members without creating accessor functions.

    public:

    Builder(const char *name)
    : _name(name) {
    }

    Builder &storageTag(const char *tag) {
      _storageTag = tag;
      return *this;
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
    Builder &idleMode(Mode<TAppState> *idleMode) {
      _idleMode = idleMode;
      return *this;
    }
    Builder &followMode(Mode<TAppState> *followMode) {
      _followMode = followMode;
      return *this;
    }
    Builder &addChild(Mode<TAppState> *child) {
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

    void lastTriggerName(char *buf, size_t size) const {
      buildTag(buf, size, "LT");
    }

    void waitName(char *buf, size_t size) const {
      buildTag(buf, size, "CW");
    }

    private:

    void buildTag(char *buf, size_t size, const char *suffix) const {
      RS_ASSERT(buf!=NULL);
      if (_storageTag==NULL) {
        RS_ASSERT(size>=1);
        *buf = '\0';
        return;
      }
      // Construct an 8 character storage tag name of the form: R[1] tag[5] suffix[2]
      RS_ASSERT(size>=9);
      RS_ASSERT(strlen(suffix)<=2);
      char tag[6];
      strncpy(tag, _storageTag, sizeof(tag));
      tag[sizeof(tag)-1] = '\0';
      sprintf(buf, "R%s%s", tag, suffix);
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

  Mode<TAppState> * const _idleMode = NULL;
  Mode<TAppState> * const _followMode = NULL;
  const std::vector<Mode<TAppState>*> _children;
  const uint8_t _childActivationLimit = 0;
  const uint8_t _childSimultaneousLimit = 0;

  const StateModFn _inspirationPred = NULL;
  const ActionFn _invokeFunction = NULL;
  const StatePredicate _requiredPred = NULL;

  private:
  uint8_t _countParents = 0;
  uint8_t _supportiveParents = 0;
  uint32_t _supportiveFrame = 0; // changeCounter value corresponding to current supportiveParents value. Alternative is to initialize _supportiveParents = _countParents before propagation.

  char _lastTriggerName[10]; // Name by which Last Trigger value is recovered from storage
  bool _accumulateWait = false;
  uint32_t _waitCumulative = 0;
  uint32_t _waitStart = 0;
  char _waitName[10]; // Name by which Cumulative Wait value is recovered from storage

  void reset() {
    _supportiveFrame = 0; // Reinitialize this count
    _stateIndex = STATE_INDEX_INITIAL;
  }

  uint32_t period() const {
    switch (_perUnit) {
      case TimeUnitDay:
        return 24 * 60 * 60 * 1000 / _perTimes;
      case TimeUnitHour:
        return 60 * 60 * 1000 / _perTimes;
      case TimeUnitNone:
      default:
        return 0;
    }
  }

  public:
  Mode(const Builder &builder);

  void deepReset() {
    reset();
    for (auto m = _children.begin(); m!=_children.end(); ++m) {
      (*m)->deepReset();
    }
  }

  uint32_t maxSleep(const TAppState &state, uint32_t ms) const;

  bool attached() const { return _stateIndex != STATE_INDEX_INITIAL; }

  void attach(RespireStateBase &state, uint32_t epochTime, RespireStore *store);

  void collect(std::vector<Mode<TAppState>*> &invokeModes, std::vector<Mode<TAppState>*> &timeDependentModes) {
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

  bool requiredState(const TAppState &state) const {
    if (!_requiredPred) {
      return true;
    }
    return _requiredPred(state);
  }

  bool inspired(const TAppState &state, const TAppState &oldState) const {
    return (_inspirationPred && _inspirationPred(state, oldState))
          || (requiredState(state) && !requiredState(oldState));
  }

  ActionFn invokeFunction() const {
    return _invokeFunction;
  }

  uint32_t invokeDelay() const {
    return _invokeDelay;
  }

  bool persistent(const TAppState &state) const;

  const char *name() const {
    return _name;
  }

  ActivationType activation(const TAppState &state, const TAppState &oldState) const;

  ModeState &modeState(TAppState &state);
  const ModeState &modeState(const TAppState &state) const;

  bool propagate(const ActivationType parentActivation, TAppState &state, const TAppState &oldState);
  bool propagateActive(const ActivationType parentActivation, const ActivationType myActivation, TAppState &state, const TAppState &oldState);

  void reset(TAppState &state) {
    // Enable mode to be used again.
    modeState(state)._invocationCount = 0;
  }

  bool isActive(const TAppState &state) const {
    return modeState(state)._startIndex!=0;
  }

  bool hitRepeatLimit(const TAppState &state) const {
    // Cannot be used again.
    return _repeatLimit!=0 && _repeatLimit<=modeState(state)._invocationCount;
  }

  bool invocationTerminated(const TAppState &state, const TAppState &oldState) const {
    return _invokeFunction!=NULL && !modeState(state)._invocationActive && modeState(oldState)._invocationActive;
  }

  uint8_t decSupportiveParents(const TAppState &state);

  bool insufficientGap(const TAppState &state) const;

  /**
   * Activate if not already active and if invocationCount has not exceeded repeatLimit
   */
  bool activate(TAppState &state);

  bool terminate(TAppState &state);

  bool expired(const TAppState &state) const;

  bool triggered(const TAppState &state) const;

  bool inspiring(ActivationType parentActivation, const TAppState &state, const TAppState &oldState) const;

  void dump(const TAppState &state) const {
    const ModeState &ms = modeState(state);
    Log.Debug("Mode: \"%20s\" [%8s][%7s] parents=%d", _name,
      (isActive(state) ? "Active" : "Inactive"),
      (requiredState(state) ? "Ready" : "Unready"),
      _countParents);
    if (_repeatLimit==0) {
      Log.Debug_(" invocations: %d,", (int)ms._invocationCount);
    }
    else {
      Log.Debug_(" invocations: %d of [%d],", (int)ms._invocationCount, (int)_repeatLimit);
    }
    if (_childSimultaneousLimit!=0) Log.Debug_(" childSimultaneous: %d,", (int)_childSimultaneousLimit);
    if (_children.size()>0) Log.Debug_(" childInspirations: %d [limit %d],", (int)ms._childInspirationCount, (int)_childActivationLimit);
    if (_perUnit != TimeUnitNone) Log.Debug_(" lastTrigger: %lu,", (long unsigned int)ms._lastTriggerMillis);
    if (_invokeFunction!=NULL) Log.Debug_(" [%11s],", ms._invocationActive ? "Running" : "Not running");
    if (_invokeDelay!=0) Log.Debug_(" invokeDelay: %d,", (int)_invokeDelay);
    if (_invokeDelay!=0 && ms._invocationActive)  Log.Debug_(" lastTrigger: %lu,", (long unsigned int)ms._lastTriggerMillis);
    if (_perUnit != TimeUnitNone || _minGapDuration!=0) Log.Debug(" tagLT=%s", _lastTriggerName);
    if (_accumulateWait) Log.Debug(" tagCW=%s", _waitName);
    Log.Debug_("\n");

    for (auto m = _children.begin(); m!=_children.end(); ++m) {
      (*m)->dump(state);
    }
  }

  void checkpoint(TAppState &state, RespireStore &store);

  private:

  void checkpoint(uint32_t now, RespireStore &store) {
    if (_accumulateWait) {
      _waitCumulative += (now - _waitStart) / 1000; // Convert ms to seconds
      _waitStart = now;
      store.store(_waitName, _waitCumulative);
    }

    for (auto m = _children.begin(); m!=_children.end(); ++m) {
      (*m)->checkpoint(now, store);
    }
  }
};

/**
 * Executor gives us an opportunity to capture the execution of actions. Used for testing.
 */
template <class TAppState>
class Executor {
  public:
  virtual void exec(typename Mode<TAppState>::ActionFn action, const TAppState &state, const TAppState &oldState, Mode<TAppState> *trigger);
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
  virtual void onChange(const TAppState &oldState, Executor<TAppState> *executor) = 0;
  virtual void didActions(const TAppState &oldState) {};
  virtual void didUpdate(const TAppState &oldState, const Mode<TAppState> &mainMode, const uint16_t holdLevel) {};

  virtual void dump(const Mode<TAppState> &mainMode) const = 0;
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
  Mode<TAppState> &_modeMain;

  Clock *_clock;
  Executor<TAppState> *_executor;
  bool _initialized = false;
  uint16_t _holdLevel = 0;

  std::vector<Mode<TAppState>*> _invokeModes;
  std::vector<Mode<TAppState>*> _timeDependentModes;

  public:

  RespireContext(TAppState &appState, Mode<TAppState> &modeMain, Clock *clock, Executor<TAppState> *executor)
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
    // Walk all nodes and restore to initial values.
    _modeMain.deepReset();
  }

  void setExecutor(Executor<TAppState> *executor) {
    _executor = executor;
  }

  const TAppState &appState() const {
    return _appState;
  }

  void init(uint32_t realTimeEpoch, RespireStore *store) {
    Log.Debug("RespireContext::init() realTime: %u" CR, realTimeEpoch);

    _appState.reset();

    _modeMain.collect(_invokeModes, _timeDependentModes);

    _clock->currentTime(realTimeEpoch);

    _modeMain.attach(_appState, realTimeEpoch, store);

    // Main is always active
    _modeMain.activate(_appState);
    // _modeMain.dump(_appState);

    TAppState reference; // Initial rev to compare to
    // reference.dump();
    onUpdate(reference);

    _initialized = true;
  }

  void init() {
    init(0, NULL);
  }

  void begin() {
    RS_ASSERT(_initialized);
    TAppState reference;
    resumeActions(reference);
  }

  void complete(Mode<TAppState> &mode, const std::function< void(TAppState&) > &updateFn = [](TAppState&) {}) {
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
  void complete(Mode<TAppState> *mode, const std::function< void(TAppState&) > &updateFn = [](TAppState&) {}) {
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

  void onUpdate(const TAppState &oldState) {
    _appState.newFrame(_clock->millis());

    _modeMain.propagate(ActivationActive, _appState, oldState);

    if (_holdLevel==0) {
      performActions(oldState);
    }

    _appState.didUpdate(oldState, _modeMain, _holdLevel);
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

    _appState.didActions(oldState);
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

#include "respire.cpp" // Load in template definitions

#endif