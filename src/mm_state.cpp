#include "mm_state.h"
#include <Logging.h>

Clock gClock;
Executor gExecutor;

// Shared
Mode ModeAttemptJoin("AttemptJoin", attemptJoin);
Mode ModeSend("Send");
  Mode ModeSendNoAck("SendNoAck", sendLocation);
  Mode ModeSendAck("SendAck", sendLocationAck);

// Main
Mode ModeMain("Main", 1);
  Mode ModeSleep("Sleep", changeSleep);
  Mode ModeLowPowerJoin("LowPowerJoin", 1);
  Mode ModeLowPowerGpsSearch("LowPowerGpsSearch", 1, MINUTES_IN_MILLIS(5), MINUTES_IN_MILLIS(5));
  Mode ModeReadAndSend("ReadAndSend");
  Mode ModeReadGps("ReadGps", readGpsLocation);
  Mode ModeLowPowerSend("LowPowerSend", 1);
  Mode ModePeriodicJoin("PeriodicJoin", 12, TimeUnitHour);
  Mode ModePeriodicSend("PeriodicSend", 6, TimeUnitHour);

std::vector<Mode*> InvokeModes;
int _static_initialization_ = []() -> int {
  ModeMain.addChild(&ModeSleep);
  ModeMain.defaultMode(&ModeSleep);
  ModeMain.addChild(&ModeLowPowerJoin);
  ModeMain.addChild(&ModeLowPowerGpsSearch);
  ModeMain.addChild(&ModeLowPowerSend);
  ModeMain.addChild(&ModePeriodicJoin);
  ModeMain.addChild(&ModePeriodicSend);

  ModeLowPowerJoin.addChild(&ModeAttemptJoin);
  ModeLowPowerSend.addChild(&ModeReadAndSend);

  ModePeriodicJoin.addChild(&ModeAttemptJoin);
  ModePeriodicSend.addChild(&ModeReadAndSend);

  ModeReadAndSend.addChild(&ModeReadGps);
  ModeReadAndSend.addChild(&ModeSend);

  ModeSend.addChild(&ModeSendAck);
  ModeSendAck.minGapDuration(DAYS_IN_MILLIS(1));
  ModeSend.addChild(&ModeSendNoAck);
  ModeSend.childActivationLimit(1);
  ModeSend.childSimultaneousLimit(1);

  ModeLowPowerJoin.requiredFunction([](const AppState &state) -> bool {
    return !state.getUsbPower() && !state.getJoined();
  });
  ModeLowPowerGpsSearch.requiredFunction([](const AppState &state) -> bool {
    return !state.getUsbPower() && state.getJoined() && !state.hasGpsFix();
  });
  ModeReadGps.requiredFunction([](const AppState &state) -> bool {
    return state.getJoined() && state.hasGpsFix();
  });
  ModeLowPowerSend.requiredFunction([](const AppState &state) -> bool {
    return !state.getUsbPower() && state.getJoined() && state.hasGpsFix();
  });

  ModePeriodicSend.requiredFunction([](const AppState &state) -> bool {
    return state.getUsbPower() && state.getJoined() && state.hasGpsFix();
  });
  ModePeriodicJoin.requiredFunction([](const AppState &state) -> bool {
    return state.getUsbPower() && !state.getJoined();
  });

  ModeSend.requiredFunction([](const AppState &state) -> bool {
    return state.getJoined() && state.hasGpsLocation();
  });

  ModeMain.collect(InvokeModes);

  return 0;
}();

void AppState::init() {
  Log.Debug("AppState::init()" CR);
  ModeMain.detach(*this);
  ModeMain.attach(*this);

  // Main is always active
  ModeMain.activate(*this);
  // ModeMain.dump(*this);

  AppState reference; // Initial rev to compare to
  // reference.dump();
  setDependent(reference);

  _initialized = true;
}

Mode::Mode(const char *name, uint8_t repeatLimit, uint32_t minDuration, uint32_t maxDuration)
: _name(name),
  _repeatLimit(repeatLimit),
  _minDuration(minDuration),
  _maxDuration(maxDuration)
{
}

Mode::Mode(const char *name, uint16_t times, TimeUnit perUnit)
: _name(name),
  _perTimes(times),
  _perUnit(perUnit)
{
}

void Mode::attach(AppState &state) {
  if (_stateIndex!=STATE_INDEX_INITIAL) {
    return;
  }
  _stateIndex = state.allocateMode();

  _supportiveFrame = 0;

  ModeState &ms = state.modeState(_stateIndex);
  ms._startIndex = 0;
  ms._startMillis = 0;
  ms._invocationCount = 0;

  for (auto m = _children.begin(); m!=_children.end(); ++m) {
    (*m)->attach(state);
  }
}

void Mode::detach(AppState &state) {
  _stateIndex = STATE_INDEX_INITIAL;

  for (auto m = _children.begin(); m!=_children.end(); ++m) {
    (*m)->detach(state);
  }
}

ModeState &Mode::modeState(AppState &state) {
  return state.modeState(_stateIndex);
}

const ModeState &Mode::modeState(const AppState &state) const {
  return state.modeState(_stateIndex);
}

bool Mode::insufficientGap(const AppState &state) const {
  if (_minGapDuration==0) {
    // We don't have this limit. Never insufficient.
    return false;
  }
  if (modeState(state)._endMillis==0) {
    // Never been run. We have no idea of gap, so no, not insufficient.
    return false;
  }
  return (state.millis() - modeState(state)._endMillis) < _minGapDuration;
}

bool Mode::expired(const AppState &state) const {
  if (!isActive(state)) {
    // Not active. Now way to expire.
    return false;
  }
  if (_maxDuration==0) {
    // No max.
    return false;
  }
  bool expired = (modeState(state)._startMillis + _maxDuration) <= state.millis();
  return expired;
}

bool Mode::triggered(const AppState &state) const {
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
  // Log.Debug("Triggered values: last=%lu, period=%lu, current=%lu\n", _lastTriggerMillis, period, currentMillis);
  bool triggered = (modeState(state)._lastTriggerMillis==0) || (modeState(state)._lastTriggerMillis + period) <= state.millis();
  return triggered;
}

bool Mode::persistent(const AppState &state) const {
  bool persist = false;
  if (_invokeFunction!=NULL) {
    // We started an external function and we stick around until it is done.
    persist |= modeState(state)._invocationActive;
  }
  if (_minDuration!=0) {
    // Once inspired we stay alive until we have lived minDuration.
    persist |= (modeState(state)._startMillis + _minDuration) > state.millis(); // Soonest expiration time is later than now
  }
  if (_perUnit!=TimeUnitNone) {
    // As long as one child has supply to be inspired, we persist.
    // Log.Debug("Checking supply of children of %s [%s]\n", name(), persist ? "persisting" : "not persisting");
    for (auto m = _children.begin(); m!=_children.end(); ++m) {
      Mode *mode = *m;
      Log.Debug(" %s invocations=%u limit=%u\n", mode->name(), mode->modeState(state)._invocationCount, mode->_repeatLimit);
      persist |= !mode->hitRepeatLimit(state);
    }
    // Log.Debug("Checking supply of children of %s [%s]\n", name(), persist ? "persisting" : "not persisting");
  }
  return persist;
}

ActivationType Mode::activation(const AppState &state, const AppState &oldState) const {
  bool now = isActive(state);
  bool old = isActive(oldState);
  if (now) {
    if (persistent(state)) {
      return triggered(state) ?  ActivationInspiring : ActivationSustaining;
    }
    else {
      return old ? ActivationActive : ActivationInspiring;
    }
  }
  else {
    return old ? ActivationExpiring : ActivationInactive;
  }
}

bool Mode::activate(AppState &state) {
  Log.Debug("Activating: %s\n", name());
  if (isActive(state)) {
    // Already active. Don't change anything.
    // Log.Debug("Already active\n");
    return false;
  }
  if (hitRepeatLimit(state)) {
    // Hit repeat limit. Don't activate.
    // Log.Debug("Repeat limit\n");
    return false;
  }
  if (insufficientGap(state)) {
    // Not enough time has passed since last invocation
    Log.Debug("Insufficient gap delay\n");
    return false;
  }
  modeState(state)._startIndex = state.changeCounter();
  modeState(state)._startMillis = state.millis();
  modeState(state)._invocationCount++;
  modeState(state)._childInspirationCount = 0;
  if (_invokeFunction!=NULL) {
    modeState(state)._invocationActive = true;
  }
  // Log.Debug("Done %u\n", modeState(state)._invocationCount);
  return true;
}

bool Mode::terminate(AppState &state) {
  Log.Debug("Terminating: %s\n", name());
  if (!isActive(state)) {
    // Already inactive. Don't change anything.
    // Log.Debug("Not active\n");
    return false;
  }
  modeState(state)._startIndex = 0; // Inactive
  modeState(state)._endMillis = state.millis();
  if (modeState(state)._invocationActive) {
    modeState(state)._invocationActive = false; // TODO: More active cancel? Probably.
  }
  return true;
}

void AppState::complete(Mode &mode, void (*updateFn)(AppState &state)) {
  // Log.Debug("----------- completed: %s [%s]\n", mode.name(), (mode.modeState(*this)._invocationActive ? "Running" : "Not running"));
  if (!mode.modeState(*this)._invocationActive) {
    return;
  }
  StateTransaction t(*this);
  if (updateFn!=NULL) {
    updateFn(*this);
  }
  AppState oldState(*this);
  mode.modeState(*this)._invocationActive = false;
  setDependent(oldState);
}

uint8_t Mode::decSupportiveParents(const AppState &state) {
  if (state.changeCounter()!=_supportiveFrame) {
    Log.Debug("Resetting parents to %d\n", (int)_countParents);
    _supportiveParents = _countParents;
    _supportiveFrame = state.changeCounter();
  }
  --_supportiveParents;
  Log.Debug("Remaining supportive parents = %d\n", (int)_supportiveParents);
  return _supportiveParents;
}

bool Mode::propagateActive(const ActivationType parentActivation, const ActivationType myActivation, AppState &state, const AppState &oldState) {

  bool barren = true;

  int limit = INT_MAX;
  int remaining = INT_MAX;

  // Figure out how many children may be inspired
  if (_childSimultaneousLimit!=0) {
    limit = _childSimultaneousLimit;
    for (auto m = _children.begin(); m!=_children.end(); ++m) {
      if ((*m)->isActive(state)) {
        --limit;
      }
    }
  }
  if (_childActivationLimit!=0) {
    // Track remaining in order to figure out if any children can be inspired in the future.
    remaining = (int)_childActivationLimit - (int)modeState(state)._childInspirationCount;
    limit = std::min(limit, remaining);
  }
  ActivationType childActivation = (limit==0) ? ActivationSustaining : myActivation;

  bool skippedDefault = false;
  for (auto m = _children.begin(); m!=_children.end(); ++m) {
    auto mode = *m;
    if (_defaultMode!=mode || childActivation==ActivationSustaining) {
      bool oldActive = mode->isActive(state);
      bool active = mode->propagate(childActivation, state, oldState);
      barren &= !active;

      if (!oldActive && active) {
        // Inspired the child.
        ++modeState(state)._childInspirationCount;
        --remaining;
        --limit;
        if (limit==0) {
          // We've reached the limit of our inspiration. Proceed with just sustaining power.
          childActivation = ActivationSustaining;
          if (skippedDefault) {
            // Now that we're in sustaining mode, propagate to _defaultMode that we skipped.
            bool active = mode->propagate(childActivation, state, oldState);
            barren &= !active;
            skippedDefault = false;
          }
        }
      }
    }
    else {
      skippedDefault |= _defaultMode==mode;
    }
  }

  if (!_children.empty() && remaining==0 && barren && !persistent(state)) {
    assert(limit==0); // If remaining is 0, limit must be, too.
    assert(!skippedDefault); // If limit is 0, logic above will have propagated to default
    assert(childActivation==ActivationSustaining);
    Log.Debug("Terminating for barren and no capacity to inspire children: %s\n", name());
    terminate(state);
  }
  else if (childActivation!=ActivationSustaining) {
    if (_defaultMode!=NULL) {
      assert(skippedDefault);
      // We have default cell. Actively inspire it if barren or kill it if not barren.
      if (barren) {
        if (limit>0) {
          Log.Debug("Activating default: %s\n", _defaultMode->name());
          bool active = _defaultMode->propagate(ActivationDefaultCell, state, oldState);
          if (active) {
            ++modeState(state)._childInspirationCount;
          }
        }
      }
      else {
        Log.Debug("Terminating default: %s\n", _defaultMode->name());
        _defaultMode->propagate(ActivationInactive, state, oldState);
      }
    }
    else { // No default Mode
      assert(!skippedDefault);
      if (barren) {
        // Barren cells lose activation, unless we are explicitly kept active as defaultCell or persistent(because periodic or min duration)
        if (parentActivation!=ActivationDefaultCell && !persistent(state)) {
          Log.Debug("Terminating for barrenness: %s\n", name());
          terminate(state);
        }
      }
    }
  }

  if (triggered(state)) {
    modeState(state)._lastTriggerMillis = state.millis();
  }

  return isActive(state);
}

bool Mode::propagate(const ActivationType parentActivation, AppState &state, const AppState &oldState) {
  // Terminating condition vs containing running Modes? Terminating condition wins.
  // Similarly, terminating condition wins against minimum active duration.
  // (If cell with minimum should persist beyond parent, use another mechanism, like detached sequence.)
  // Parent not inspiring, all children get deactivated (unless shared).

  // Log.Debug("Propagating: %s with parent %u\n", name(), parentActivation);
  // dump(state);

  if (isActive(state)) {
    // Active. Should terminate?
    if (expired(state)
        || invocationTerminated(state, oldState)
        || !requiredState(state)) {
      // Regardless of other parents, this cell cannot be active.
      terminate(state);
    }
    else if (parentActivation==ActivationExpiring || parentActivation==ActivationInactive) {
      // Record that parent not supportive.
      Log.Debug("Checking active %s for termination\n", name());
      if (0==decSupportiveParents(state)) {
        terminate(state);
      }
      else {
        // Don't propagate until all parent statuses are determined.
        return false; // Parent is dead/dying, so it won't be doing any barren logic.
      }
    }
  }
  else {
    // Not active. Should activate?
    if (parentActivation==ActivationDefaultCell
        || (parentActivation==ActivationInspiring && requiredState(state))
        || (parentActivation==ActivationActive && requiredState(state) && !requiredState(oldState))) {
      // Either parent activation or requiredState (or both) just transitioned to true.
      if (parentActivation==ActivationInspiring) {
        reset(state); // Reset invocation count. We're in a fresh parent!
      }
      activate(state);
    }
  }

  ActivationType myActivation = activation(state, oldState);
  if (isActive(state)) { // Yes, re-call isActive because it may have changed above
    return propagateActive(parentActivation, myActivation, state, oldState);
  }
  else {
    // We don't care about barren & default processing if we're not active - they all get shut down
    for (auto m = _children.begin(); m!=_children.end(); ++m) {
      (*m)->propagate(myActivation, state, oldState);
    }
    return false;
  }
}

void Executor::exec(ListenerFn listener, const AppState &state, const AppState &oldState, Mode *mode) {
  // TODO Mode needs to be notified of completion
  listener(state, oldState);
}
