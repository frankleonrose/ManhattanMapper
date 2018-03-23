#include "mm_state.h"
#include <Logging.h>

// Shared
Mode ModeAttemptJoin(Mode::Builder("AttemptJoin").invokeFn(attemptJoin));
Mode ModeSend(Mode::Builder("Send")
    .childActivationLimit(1)
    .childSimultaneousLimit(1)
    .addChild(&ModeSendAck)
    .addChild(&ModeSendNoAck)
    .requiredPred([](const AppState &state) -> bool {
      return state.hasRecentGpsLocation();
    }));
  Mode ModeSendNoAck(Mode::Builder("SendNoAck").invokeFn(sendLocation));
  Mode ModeSendAck(Mode::Builder("SendAck")
                    .invokeFn(sendLocationAck)
                    .minGapDuration(DAYS_IN_MILLIS(1)));

// Main
Mode ModeMain(Mode::Builder("Main")
              .repeatLimit(1)
              .idleMode(&ModeSleep)
              .addChild(&ModeSleep)
              .addChild(&ModeLowPowerJoin)
              .addChild(&ModeLowPowerGpsSearch)
              .addChild(&ModeLowPowerSend)
              .addChild(&ModePeriodicJoin)
              .addChild(&ModePeriodicSend));
  Mode ModeSleep(Mode::Builder("Sleep").invokeFn(changeSleep));
  Mode ModeLowPowerJoin(Mode::Builder("LowPowerJoin")
      .repeatLimit(1)
      .addChild(&ModeAttemptJoin)
      .requiredPred([](const AppState &state) -> bool {
        return !state.getUsbPower() && !state.getJoined();
      }));
  Mode ModeLowPowerGpsSearch(Mode::Builder("LowPowerGpsSearch")
      .repeatLimit(1)
      .minDuration(MINUTES_IN_MILLIS(5))
      .maxDuration(MINUTES_IN_MILLIS(5))
      .requiredPred([](const AppState &state) -> bool {
        return !state.getUsbPower() && state.getJoined() && !state.hasGpsFix();
      }));
  Mode ModeReadAndSend(Mode::Builder("ReadAndSend")
      .addChild(&ModeReadGps)
      .addChild(&ModeSend)
      .addChild(&ModeLogGps)
      .requiredPred([](const AppState &state) -> bool {
        return state.getJoined();
      }));
  Mode ModeReadGps(Mode::Builder("ReadGps")
      .invokeFn(readGpsLocation)
      .requiredPred([](const AppState &state) -> bool {
        return state.hasGpsFix();
      }));
  Mode ModeLogGps(Mode::Builder("LogGps")
      .invokeFn(writeLocation)
      .followMode(&ModeSend));
  Mode ModeLowPowerSend(Mode::Builder("LowPowerSend")
      .repeatLimit(1)
      .addChild(&ModeReadAndSend)
      .requiredPred([](const AppState &state) -> bool {
        return !state.getUsbPower() && state.getJoined() && state.hasGpsFix();
      }));
  Mode ModePeriodicJoin(Mode::Builder("PeriodicJoin")
      .periodic(12, TimeUnitHour)
      .addChild(&ModeAttemptJoin)
      .requiredPred([](const AppState &state) -> bool {
        return state.getUsbPower() && !state.getJoined();
      }));
  Mode ModePeriodicSend(Mode::Builder("PeriodicSend")
      .periodic(6, TimeUnitHour)
      .addChild(&ModeReadAndSend)
      .requiredPred([](const AppState &state) -> bool {
        return state.getUsbPower() && state.getJoined() && state.hasGpsFix();
      }));

