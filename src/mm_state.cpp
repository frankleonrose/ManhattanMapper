#include <Arduino.h>
#include "mm_state.h"

Clock gClock;
Executor gExecutor;

// Shared
Mode ModeAttemptJoin("AttemptJoin", 0);
Mode ModeSend("Send", 0);

// Main
Mode ModeMain(NULL, 1);
  Mode ModeSleep("Sleep", 0);
  Mode ModeLowPowerJoin("LowPowerJoin", 1);
  Mode ModeLowPowerGpsSearch("LowPowerGpsSearch", 1, MINUTES_IN_MILLIS(5));
  Mode ModePeriodicSend("PeriodicSend", 6, TimeUnitHour);

void AppState::init() {
  ModeMain.attach(*this);
  ModeAttemptJoin.attach(*this);
  ModeLowPowerJoin.attach(*this);
  ModeLowPowerGpsSearch.attach(*this);
  ModeSleep.attach(*this);
  ModePeriodicSend.attach(*this);
  ModeSend.attach(*this);

  // Main is always active
  ModeMain.activate(*this, _changeCounter, _clock->millis(), &ModeMain);

  AppState reference; // Initial rev to compare to
  // reference.dump();
  setDependent(reference);
  onChange(reference);
}

Mode::Mode(const char *name, uint8_t repeatLimit, uint32_t maxDuration)
: _name(name),
  _repeatLimit(repeatLimit),
  _maxDuration(maxDuration),
  _perTimes(0),
  _perUnit(TimeUnitNone)
{
}

Mode::Mode(const char *name, uint16_t times, TimeUnit perUnit)
: _name(name),
  _repeatLimit(0),
  _maxDuration(0),
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
  ms._enclosing = NULL;
}

ModeState &Mode::modeState(AppState &state) {
  return state.modeState(_stateIndex);
}

const ModeState &Mode::modeState(const AppState &state) const {
  return state.modeState(_stateIndex);
}

void Executor::exec(ListenerFn listener, const AppState &state, const AppState &oldState, Mode *mode) {
  // TODO Mode needs to be notified of completion, maybe?
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
