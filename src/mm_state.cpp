#include "mm_state.h"
#include <Logging.h>

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

  return 0;
}();

