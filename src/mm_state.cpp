#include <Arduino.h>
#undef min
#undef max
#include <vector>
#include "mm_state.h"

Clock gClock;
Executor gExecutor;

// Shared
Mode ModeAttemptJoin("AttemptJoin", attemptJoin);
Mode ModeSend("Send", sendLocation);

// Main
Mode ModeMain(NULL, 1);
  Mode ModeSleep("Sleep", changeSleep);
  Mode ModeLowPowerJoin("LowPowerJoin", 1);
  Mode ModeLowPowerGpsSearch("LowPowerGpsSearch", 1, MINUTES_IN_MILLIS(5), MINUTES_IN_MILLIS(5));
  Mode ModePeriodicSend("PeriodicSend", 6, TimeUnitHour);

std::vector<Mode*> InvokeModes;
int _force_initialization_ = []() -> int {
  ModeMain.addChild(&ModeSleep);
  ModeMain.defaultMode(&ModeSleep);
  ModeMain.addChild(&ModeLowPowerJoin);
  ModeMain.addChild(&ModeLowPowerGpsSearch);
  ModeMain.addChild(&ModePeriodicSend);
  ModeLowPowerJoin.addChild(&ModeAttemptJoin);
  ModePeriodicSend.addChild(&ModeSend);

  InvokeModes.push_back(&ModeSleep);
  InvokeModes.push_back(&ModeAttemptJoin);
  InvokeModes.push_back(&ModeSend);

  return 0;
}();

void AppState::init() {
  ModeMain.attach(*this);
  ModeAttemptJoin.attach(*this);
  ModeLowPowerJoin.attach(*this);
  ModeLowPowerJoin.requiredFunction([](const AppState &state) -> bool {
    return !state._usbPower && !state._joined;
  });
  ModeLowPowerGpsSearch.attach(*this);
  ModeLowPowerGpsSearch.requiredFunction([](const AppState &state) -> bool {
    return !state._usbPower && state._joined && !state._gpsFix;
  });

  ModeSleep.attach(*this);
  ModePeriodicSend.attach(*this);
  ModePeriodicSend.requiredFunction([](const AppState &state) -> bool {
    return state._usbPower && state._joined;
  });
  ModeSend.attach(*this);

  // Main is always active
  ModeMain.activate(*this);
  // ModeMain.dump(*this);

  AppState reference; // Initial rev to compare to
  // reference.dump();
  setDependent(reference);
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
  _stateIndex = state.allocateMode();

  ModeState &ms = state.modeState(_stateIndex);
  ms._startIndex = 0;
  ms._startMillis = 0;
  ms._invocationCount = 0;
  // ms._enclosing = NULL;
}

ModeState &Mode::modeState(AppState &state) {
  return state.modeState(_stateIndex);
}

const ModeState &Mode::modeState(const AppState &state) const {
  return state.modeState(_stateIndex);
}

bool Mode::persistent(const AppState &state) const {
  bool persist = false;
  if (_invokeFunction!=NULL) {
    // We started an external function and we stick around until it is done.
    persist |= modeState(state)._invocationActive;
  }
  else if (_minDuration!=0) {
    // Once inspired we stay alive until we have lived minDuration.
    persist |= (modeState(state)._startMillis + _minDuration) > state.millis(); // Soonest expiration time is later than now
  }
  else if (_perUnit!=TimeUnitNone) {
    // As long as one child has supply to be inspired, we persist.
    // printf("Checking supply of children of %s [%s]\n", name(), persist ? "persisting" : "not persisting");
    for (auto m = _children.begin(); m!=_children.end(); ++m) {
      Mode *mode = *m;
      // printf(" %s invocations=%u limit=%u\n", mode->name(), mode->modeState(state)._invocationCount,mode->_repeatLimit);
      persist |= (mode->_repeatLimit==0) || (mode->modeState(state)._invocationCount < mode->_repeatLimit);
    }
    // printf("Checking supply of children of %s [%s]\n", name(), persist ? "persisting" : "not persisting");
  }
  return persist;
}

ActivationType Mode::activation(const AppState &state, const AppState &oldState) const {
  bool now = isActive(state);
  bool old = isActive(oldState);
  if (now) {
    return old ? ActivationActive : ActivationInspiring;
  }
  else {
    return old ? ActivationExpiring : ActivationInactive;
  }
}

bool Mode::activate(AppState &state) {
  // printf("Activating: %s - ", name());
  if (isActive(state)) {
    // Already active. Don't change anything.
    // printf("Already active\n");
    return false;
  }
  if (_repeatLimit!=0 && modeState(state)._invocationCount>=_repeatLimit) {
    // Hit repeat limit. Don't activate.
    // printf("Repeat limit\n");
    return false;
  }
  modeState(state)._startIndex = state.changeCounter();
  modeState(state)._startMillis = state.millis();
  modeState(state)._invocationCount++;
  if (_invokeFunction!=NULL) {
    modeState(state)._invocationActive = true;
  }
  // printf("Done %u\n", modeState(state)._invocationCount);
  return true;
}

bool Mode::terminate(AppState &state) {
  // printf("Terminating: %s\n", name());
  return setInactive(state, modeState(state)._startIndex, state.millis());
}

bool Mode::propagate(const ActivationType parentActivation, AppState &state, const AppState &oldState) {
  // Terminating condition vs containing running Modes? Terminating condition wins.
  // Similarly, terminating condition wins against minimum active duration.
  // (If cell with minimum should persist beyond parent, use another mechanism, like detached sequence.)
  // Parent not inspiring, all children get deactivated (unless shared).

  // printf("Propagating: %s with parent %u\n", name(), parentActivation);
  // dump(state);

  if (isActive(state) && !requiredState(state)) {
    // Activate and unable to be.
    terminate(state);
  }
  else if (!isActive(state) &&
      (parentActivation==ActivationDefaultCell
      || (parentActivation==ActivationInspiring && requiredState(state))
      || (parentActivation==ActivationActive && requiredState(state) && !requiredState(oldState)))) {
    // Either parent activation or requiredState (or both) just transitioned to true.
    if (parentActivation==ActivationInspiring) {
      reset(state); // Reset invocation count. We're in a fresh parent!
    }
    activate(state);
  }
  else if (isActive(state) && (parentActivation==ActivationExpiring || parentActivation==ActivationInactive)) {
    // Active but parent not supportive. Record that parent not supportive.
    if (state.changeCounter()!=_supportiveFrame) {
      _supportiveParents = _countParents;
    }
    --_supportiveParents;
    if (_supportiveParents==0) {
      terminate(state);
    }
  }

  bool barren = true;
  ActivationType myActivation = activation(state, oldState);
  bool imActive = isActive(state);
  for (auto m = _children.begin(); m!=_children.end(); ++m) {
    auto mode = *m;
    if (_defaultMode!=mode || !imActive) {
      barren &= !mode->propagate(myActivation, state, oldState);
    }
  }

  if (imActive) {
    // We don't care about children if we're not active - they all get shut down
    if (_defaultMode!=NULL) {
      // We have default cell. Actively inspire it if barren or kill it if not barren.
      if (barren) {
        // printf("Activating default: %s\n", _defaultMode->name());
        _defaultMode->propagate(ActivationDefaultCell, state, oldState);
      }
      else {
        // printf("Terminating default: %s\n", _defaultMode->name());
        _defaultMode->propagate(ActivationInactive, state, oldState);
      }
    }
    else { // No default Mode
      if (barren) {
        // Barren cells lose activation, unless we are explicitly kept active as defaultCell
        if (parentActivation!=ActivationDefaultCell && !persistent(state)) {
          // printf("Terminating for barrenness: %s\n", name());
          terminate(state);
        }
      }
    }
  }

  return isActive(state);
}

void Executor::exec(ListenerFn listener, const AppState &state, const AppState &oldState, Mode *mode) {
  // TODO Mode needs to be notified of completion
  listener(state, oldState);
}

void changeGpsPower(const AppState &state, const AppState &oldState) {
  // Reify GpsPower value
  digitalWrite(GPS_POWER_PIN, state.getGpsPower());
}

void attemptJoin(const AppState &state, const AppState &oldState) {
  // Enter the AttempJoin state, which is to say, call lorawan.join()
}

void changeSleep(const AppState &state, const AppState &oldState) {
  // Enter or exit Sleep state
}

void sendLocation(const AppState &state, const AppState &oldState) {
  // Send location
}
