#ifndef RESPIRE_CPP
#define RESPIRE_CPP

#include <Logging.h>
#include "respire.h"

#if UNIT_TEST
#define RANDOM nativeRandom
#else
#define RANDOM random
#endif

template <class TAppState>
Mode<TAppState>::Mode(const Builder &builder)
: _name(builder._name),
  _repeatLimit(builder._repeatLimit),
  _minDuration(builder._minDuration),
  _maxDuration(builder._maxDuration),
  _minGapDuration(builder._minGapDuration),
  _invokeDelay(builder._invokeDelay),
  _perTimes(builder._perTimes),
  _perUnit(builder._perUnit),
  _idleMode(builder._idleMode),
  _followMode(builder._followMode),
  _children(builder._children),
  _childActivationLimit(builder._childActivationLimit),
  _childSimultaneousLimit(builder._childSimultaneousLimit),
  _inspirationPred(builder._inspirationPred),
  _invokeFunction(builder._invokeFunction),
  _requiredPred(builder._requiredPred)
{
  // Don't do anything with referred Modes (children, etc) here because they may not yet be initialized.
  builder.lastTriggerName(_lastTriggerName, sizeof(_lastTriggerName));
  builder.waitName(_waitName, sizeof(_waitName));
  _accumulateWait = strlen(_waitName)==8; // Has a valid waitName
}

template <class TAppState>
void Mode<TAppState>::attach(RespireStateBase &state, uint32_t nowEpoch, RespireStore *store) {
  if (_stateIndex!=STATE_INDEX_INITIAL) {
    ++_countParents;
    return;
  }
  _stateIndex = state.allocateMode();

  _countParents = 1;
  _supportiveFrame = 0;

  ModeState &ms = state.modeState(_stateIndex);
  ms._startIndex = 0;
  ms._startMillis = 0;
  ms._invocationCount = 0;
  ms._lastTriggerMillis = 0;
  _waitCumulative = 0;

  if (store!=NULL && (_perUnit!=TimeUnitNone || _minGapDuration!=0)) {
    // Configuration wherein we want to restore the lastTriggered time from storage
    uint32_t lastTriggeredEpoch = 0;
    store->load(_lastTriggerName, &lastTriggeredEpoch);
    store->load(_waitName, &_waitCumulative);
    if (nowEpoch==0 || lastTriggeredEpoch==0 || lastTriggeredEpoch > nowEpoch) {
      // No absolute time known. Therefore, assign random point in the past as the last time.
      // Push random time back by known cumulative wait time if that is available.
      // As long as wait time is stored periodically by called checkpoint(),
      // the periodic task will have an increased chance of running despite no fixed time reference.
      ms._lastTriggerMillis = state.millis() - 1000 * _waitCumulative - RANDOM(period());
    }
    else {
      // Compare current time to stored last triggered time.
      ms._lastTriggerMillis = state.millis() - 1000 * (nowEpoch - lastTriggeredEpoch);
    }
  }


  for (auto m = _children.begin(); m!=_children.end(); ++m) {
    (*m)->attach(state, nowEpoch, store);
  }
}

template <class TAppState>
ModeState &Mode<TAppState>::modeState(TAppState &state) {
  return state.modeState(_stateIndex);
}

template <class TAppState>
const ModeState &Mode<TAppState>::modeState(const TAppState &state) const {
  return state.modeState(_stateIndex);
}

template <class TAppState>
bool Mode<TAppState>::insufficientGap(const TAppState &state) const {
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

template <class TAppState>
bool Mode<TAppState>::expired(const TAppState &state) const {
  if (!isActive(state)) {
    // Not active. Now way to expire.
    return false;
  }
  if (_maxDuration==0) {
    // No max.
    return false;
  }
  bool expired = (modeState(state)._startMillis + _maxDuration) <= state.millis();
  // Log.Debug("%s expiry=%d vs %d %s\n", name(), (modeState(state)._startMillis + _maxDuration), state.millis(), expired ? "EXPIRED" : "OK");
  return expired;
}

template <class TAppState>
bool Mode<TAppState>::triggered(const TAppState &state) const {
  if (!isActive(state)) {
    // Not active. Now way to trigger.
    return false;
  }
  if (_invokeDelay) {
    // Log.Debug("InvokeDelay triggers %T\n", (modeState(state)._startMillis + _invokeDelay) <= state.millis());
    return (modeState(state)._startMillis + _invokeDelay) <= state.millis();
  }
  uint32_t p = period();
  if (p==0) {
    return false;
  }
  // Log.Debug("Triggered values: last=%lu, period=%lu, current=%lu\n", _lastTriggerMillis, p, currentMillis);
  bool triggered = (modeState(state)._lastTriggerMillis==0) || (modeState(state)._lastTriggerMillis + p) <= state.millis();
  return triggered;
}

template <class TAppState>
bool Mode<TAppState>::persistent(const TAppState &state) const {
  bool persist = false;
  if (_invokeFunction!=NULL) {
    // We started an external function and we stick around until it is done.
    // (invokeDelay'd modes will fall in here, too. _invocationActive is set true at inspiration.)
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
      Log.Debug(" %s invocations=%d limit=%d\n", mode->name(), (int)mode->modeState(state)._invocationCount, (int)mode->_repeatLimit);
      persist |= !mode->hitRepeatLimit(state);
    }
    // Log.Debug("Checking supply of children of %s [%s]\n", name(), persist ? "persisting" : "not persisting");
  }
  return persist;
}

template <class TAppState>
uint32_t Mode<TAppState>::maxSleep(const TAppState &state, uint32_t ms) const {
  if (!isActive(state)) {
    // Not active, so none of our children are active, either.
    return ms;
  }

  if (_maxDuration!=0) {
    // Gonna expire at some point.
    const uint32_t expiry = modeState(state)._startMillis + _maxDuration;
    uint32_t expiresIn = 0;
    if (expiry>state.millis()) {
      expiresIn = expiry - state.millis();
    }
    ms = std::min(expiresIn, ms);
  }

  if (_invokeDelay) {
    // Will be invoked sometime later.
    const uint32_t invoke = modeState(state)._startMillis + _invokeDelay;
    uint32_t invokedIn = 0;
    if (invoke>state.millis()) {
      invokedIn = invoke - state.millis();
    }
    ms = std::min(invokedIn, ms);
  }

  if (_perUnit!=TimeUnitNone) {
    const uint32_t p = period();
    const uint32_t last = modeState(state)._lastTriggerMillis;
    if (last==0) {
      ms = 0; // Going to be triggered immediately
    }
    else {
      // Going to be triggered at last + p
      const uint32_t trigger = last + p;
      uint32_t triggeredIn = 0;
      if (trigger>state.millis()) {
        triggeredIn = trigger - state.millis();
      }
      ms = std::min(triggeredIn, ms);
    }
  }

  for (auto m = _children.begin(); m!=_children.end(); ++m) {
    ms = (*m)->maxSleep(state, ms);
  }
  return ms;
}

template <class TAppState>
bool Mode<TAppState>::inspiring(ActivationType parentActivation, const TAppState &state, const TAppState &oldState) const {
  if (!requiredState(state)) {
    return false;
  }
  if (_followMode==NULL) {
    // Not following...
    {
      return (parentActivation==ActivationIdleCell
            ||  parentActivation==ActivationInspiring // Parent just activated
            || ((parentActivation==ActivationActive || parentActivation==ActivationSustaining)
                && inspired(state, oldState))); // Required just became true
    }
  }
  else {
    // Following...
    return    (!_followMode->isActive(state) && _followMode->isActive(oldState)) // Prior terminated
          &&  (parentActivation==ActivationInspiring || parentActivation==ActivationActive || parentActivation==ActivationSustaining);
  }
}

template <class TAppState>
ActivationType Mode<TAppState>::activation(const TAppState &state, const TAppState &oldState) const {
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

template <class TAppState>
bool Mode<TAppState>::activate(TAppState &state) {
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
    modeState(state)._lastTriggerMillis = 0;
  }
  // Log.Debug("Done %u\n", modeState(state)._invocationCount);
  return true;
}

template <class TAppState>
bool Mode<TAppState>::terminate(TAppState &state) {
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

template <class TAppState>
uint8_t Mode<TAppState>::decSupportiveParents(const TAppState &state) {
  if (state.changeCounter()!=_supportiveFrame) {
    // RS_ASSERT(0 < _countParents);
    // Log.Debug("Resetting parents to %d\n", (int)_countParents);
    _supportiveParents = _countParents;
    _supportiveFrame = state.changeCounter();
  }
  --_supportiveParents;
  // Log.Debug("Remaining supportive parents = %d\n", (int)_supportiveParents);
  return _supportiveParents;
}

template <class TAppState>
bool Mode<TAppState>::propagateActive(const ActivationType parentActivation, const ActivationType myActivation, TAppState &state, const TAppState &oldState) {

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

  bool skippedIdleCell = false;
  for (auto m = _children.begin(); m!=_children.end(); ++m) {
    auto mode = *m;
    if (_idleMode!=mode || childActivation==ActivationSustaining) {
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
          if (skippedIdleCell) {
            // Now that we're in sustaining mode, propagate to _idleMode that we skipped.
            bool active = mode->propagate(childActivation, state, oldState);
            barren &= !active;
            skippedIdleCell = false;
          }
        }
      }
    }
    else {
      skippedIdleCell |= _idleMode==mode;
    }
  }

  if (!_children.empty() && remaining==0 && barren && !persistent(state)) {
    RS_ASSERT(limit==0); // If remaining is 0, limit must be, too.
    RS_ASSERT(!skippedIdleCell); // If limit is 0, logic above will have propagated to idleCell
    RS_ASSERT(childActivation==ActivationSustaining);
    Log.Debug("Terminating for barren and no capacity to inspire children: %s\n", name());
    terminate(state);
  }
  else if (childActivation!=ActivationSustaining) {
    if (_idleMode!=NULL) {
      RS_ASSERT(skippedIdleCell);
      // We have idle cell. Actively inspire it if barren or kill it if not barren.
      if (barren) {
        if (limit>0) {
          Log.Debug("Activating idle: %s\n", _idleMode->name());
          bool active = _idleMode->propagate(ActivationIdleCell, state, oldState);
          if (active) {
            ++modeState(state)._childInspirationCount;
          }
        }
      }
      else {
        Log.Debug("Terminating idle: %s\n", _idleMode->name());
        _idleMode->propagate(ActivationInactive, state, oldState);
      }
    }
    else { // No idle Mode
      RS_ASSERT(!skippedIdleCell);
      if (barren) {
        // Barren cells lose activation, unless we are explicitly kept active as idleCell or persistent(because periodic or min duration)
        if (parentActivation!=ActivationIdleCell && !persistent(state)) {
          Log.Debug("Terminating for barrenness: %s\n", name());
          terminate(state);
        }
      }
    }
  }

  if (triggered(state)) {
    modeState(state)._lastTriggerMillis = state.millis();
    // Restart cumulative wait from this trigger event.
    _waitCumulative = 0;
    _waitStart = state.millis();
  }

  return isActive(state);
}

template <class TAppState>
bool Mode<TAppState>::propagate(const ActivationType parentActivation, TAppState &state, const TAppState &oldState) {
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
      // Log.Debug("%s: expired: %T, invocationTerminated: %T, !required: %T\n", name(), expired(state), invocationTerminated(state, oldState), !requiredState(state));
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
    RS_ASSERT(!(parentActivation==ActivationIdleCell && _followMode!=NULL)); // We don't currently support idleModes that are also followers.
    if (inspiring(parentActivation, state, oldState)) {
      // Either parent activation or requiredState (or both) just transitioned to true.
      if (parentActivation==ActivationInspiring) {
        reset(state); // Reset invocation count. We're in a fresh parent!
      }
      activate(state);
    }
  }

  ActivationType myActivation = activation(state, oldState);
  bool active = false;
  if (isActive(state)) { // Yes, re-call isActive because it may have changed above
    active = propagateActive(parentActivation, myActivation, state, oldState);
  }
  else {
    // We don't care about barren & idle processing if we're not active - they all get shut down
    for (auto m = _children.begin(); m!=_children.end(); ++m) {
      (*m)->propagate(myActivation, state, oldState);
    }
    active = false;
  }
  // Log.Debug("Leaving propagate: %s active: %T\n", name(), active);
  return active;
}

template <class TAppState>
void Mode<TAppState>::checkpoint(TAppState &state, RespireStore &store) {
  store.beginTransaction();
  checkpoint(state.millis(), store);
  store.endTransaction();
}

template <class TAppState>
void Executor<TAppState>::exec(typename Mode<TAppState>::ActionFn listener, const TAppState &state, const TAppState &oldState, Mode<TAppState> *triggeringMode) {
  listener(state, oldState, triggeringMode);
}

#endif // RESPIRE_CPP
