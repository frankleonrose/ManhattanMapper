#include <Arduino.h>
#include "mm_state.h"

Clock gClock;
Executor gExecutor;

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
