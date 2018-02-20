#include <Arduino.h>
#include "mm_state.h"

#define GPS_POWER_PIN 50

Clock gClock;
Executor gExecutor;

void Executor::exec(ListenerFn listener, const AppState &state, const AppState &oldState, Mode *mode) {
  listener(state, oldState);
}

void onChangeGpsPower(const AppState &state, const AppState &oldState) {
  // Reify GpsPower value
  digitalWrite(GPS_POWER_PIN, state.getGpsPower());
}

void onAttemptJoin(const AppState &state, const AppState &oldState) {
  // Enter the AttempJoin state, which is to say, call lorawan.join()
}
