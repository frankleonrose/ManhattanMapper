#include "mm_state.h"
#include <Logging.h>

// Shared
Mode<AppState> ModeAttemptJoin(Mode<AppState>::Builder("AttemptJoin").invokeFn(attemptJoin));
Mode<AppState> ModeSend(Mode<AppState>::Builder("Send")
    .childActivationLimit(1)
    .childSimultaneousLimit(1)
    .addChild(&ModeSendAck)
    .addChild(&ModeSendNoAck)
    .requiredPred([](const AppState &state) -> bool {
      return state.hasRecentGpsLocation();
    }));
  Mode<AppState> ModeSendNoAck(Mode<AppState>::Builder("SendNoAck").invokeFn(sendLocation));
  Mode<AppState> ModeSendAck(Mode<AppState>::Builder("SendAck")
                    .invokeFn(sendLocationAck)
                    .minGapDuration(DAYS_IN_MILLIS(1)));

Mode<AppState> ModeMain(Mode<AppState>::Builder("Main")
              .repeatLimit(1)
              .addChild(&ModeDisplay)
              .addChild(&ModeFunctional));
  Mode<AppState> ModeDisplay(Mode<AppState>::Builder("Display")
      .idleMode(&ModeDisplayBlank)
      .inspirationPred([](const AppState &state, const AppState &oldState) -> bool {
        return (state.field()!=oldState.field())                            // Field changes
            || (state.buttonAny() && !oldState.buttonAny());                // Any button is pressed
      })
      .addChild(&ModeDisplayBlank)
      .addChild(&ModeDisplayStatus)
      .addChild(&ModeDisplayParameters)
      .addChild(&ModeDisplayErrors));
    Mode<AppState> ModeDisplayBlank(Mode<AppState>::Builder("DisplayBlank")
        .invokeFn(displayBlank)
        .invokeDelay(MINUTES_IN_MILLIS(1)));
    Mode<AppState> ModeDisplayStatus(Mode<AppState>::Builder("DisplayStatus")
        .invokeFn(displayStatus)
        .requiredPred([](const AppState &state) -> bool {
          return state.page()==0;
        })
        .inspirationPred([](const AppState &state, const AppState &oldState) -> bool {
          return (state.field()!=oldState.field())
              || (state.redisplayRequested()!=oldState.redisplayRequested());  // redisplayRequested changes
        }));
    Mode<AppState> ModeDisplayParameters(Mode<AppState>::Builder("DisplayParameters")
        .invokeFn(displayParameters)
        .requiredPred([](const AppState &state) -> bool {
          return state.page()==1;
        })
        .inspirationPred([](const AppState &state, const AppState &oldState) -> bool {
          return (state.field()!=oldState.field())
              || (state.redisplayRequested()!=oldState.redisplayRequested());  // redisplayRequested changes
        }));
    Mode<AppState> ModeDisplayErrors(Mode<AppState>::Builder("DisplayErrors")
        .invokeFn(displayErrors)
        .requiredPred([](const AppState &state) -> bool {
          return state.page()==2;
        })
        .inspirationPred([](const AppState &state, const AppState &oldState) -> bool {
          // Log.Debug("%s inspirationPred: %d != %d\n", ModeDisplayErrors.name(), (int)state.field(), (int)oldState.field());
          return (state.field()!=oldState.field())
              || (state.redisplayRequested()!=oldState.redisplayRequested());  // redisplayRequested changes
        }));

Mode<AppState> ModeFunctional(Mode<AppState>::Builder("Functional")
              .idleMode(&ModeSleep)
              .addChild(&ModeSleep)
              .addChild(&ModeLowPowerJoin)
              .addChild(&ModeLowPowerGpsSearch)
              .addChild(&ModeLowPowerSend)
              .addChild(&ModePeriodicJoin)
              .addChild(&ModePeriodicSend));
  Mode<AppState> ModeSleep(Mode<AppState>::Builder("Sleep").invokeFn(changeSleep));
  Mode<AppState> ModeLowPowerJoin(Mode<AppState>::Builder("LowPowerJoin")
      .repeatLimit(1)
      .addChild(&ModeAttemptJoin)
      .requiredPred([](const AppState &state) -> bool {
        return !state.getUsbPower() && !state.getJoined();
      }));
  Mode<AppState> ModeLowPowerGpsSearch(Mode<AppState>::Builder("LowPowerGpsSearch")
      .repeatLimit(1)
      .minDuration(MINUTES_IN_MILLIS(5))
      .maxDuration(MINUTES_IN_MILLIS(5))
      .requiredPred([](const AppState &state) -> bool {
        return !state.getUsbPower() && state.getJoined() && !state.hasGpsFix();
      }));
  Mode<AppState> ModeReadAndSend(Mode<AppState>::Builder("ReadAndSend")
      .addChild(&ModeReadGps)
      .addChild(&ModeSend)
      .addChild(&ModeLogGps)
      .requiredPred([](const AppState &state) -> bool {
        return state.getJoined();
      }));
  Mode<AppState> ModeReadGps(Mode<AppState>::Builder("ReadGps")
      .invokeFn(readGpsLocation)
      .requiredPred([](const AppState &state) -> bool {
        return state.hasGpsFix();
      }));
  Mode<AppState> ModeLogGps(Mode<AppState>::Builder("LogGps")
      .invokeFn(writeLocation)
      .followMode(&ModeSend));
  Mode<AppState> ModeLowPowerSend(Mode<AppState>::Builder("LowPowerSend")
      .repeatLimit(1)
      .addChild(&ModeReadAndSend)
      .requiredPred([](const AppState &state) -> bool {
        return !state.getUsbPower() && state.getJoined() && state.hasGpsFix();
      }));
  Mode<AppState> ModePeriodicJoin(Mode<AppState>::Builder("PeriodicJoin")
      .periodic(12, TimeUnitHour)
      .addChild(&ModeAttemptJoin)
      .requiredPred([](const AppState &state) -> bool {
        return state.getUsbPower() && !state.getJoined();
      }));
  Mode<AppState> ModePeriodicSend(Mode<AppState>::Builder("PeriodicSend")
      .periodic(6, TimeUnitHour)
      .addChild(&ModeReadAndSend)
      .requiredPred([](const AppState &state) -> bool {
        return state.getUsbPower() && state.getJoined() && state.hasGpsFix();
      }));

