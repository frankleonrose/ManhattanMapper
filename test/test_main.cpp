#include <Arduino.h>
#include <unity.h>
#include <vector>
#include <Logging.h>

#include "mm_state.h"

#define UNIT_TEST
#ifdef UNIT_TEST

void printFn(const char c) {
  printf("%c", c);
}

// void setUp(void) {
// // set stuff up here
// }

// void tearDown(void) {
// // clean stuff up here
// }

class TestClock : public Clock {
  unsigned long _millis = 100000;

  public:
  virtual unsigned long millis() {
    return _millis;
  }

  void advanceSeconds(uint32_t s) {
    _millis += 1000 * s;
  }
};

#define FNAME(fn) {fn, #fn}
static struct {
  ActionFn fn;
  const char *name;
} _names[] = {
  FNAME(changeGpsPower),
  FNAME(attemptJoin),
  FNAME(changeSleep),
  FNAME(sendLocation),
  FNAME(sendLocationAck),
  FNAME(readGpsLocation),
  FNAME(writeLocation),
  FNAME(displayBlank),
  FNAME(displayStatus),
  FNAME(displayParameters),
  FNAME(displayErrors),
};

class TestExecutor : public Executor {
  std::vector<ActionFn> _expected;
  std::vector<ActionFn> _called;
  public:
  TestExecutor(ActionFn expected, ...) {
    va_list args;
    va_start(args, expected);
    while (expected!=NULL) {
      Log.Debug(F("Expect: %p %s\n"), expected, name(expected));
      _expected.push_back(expected);
      expected = va_arg(args, ActionFn);
    }
    va_end(args);
  }

  TestExecutor(ActionFn expected, va_list &args) {
    while (expected!=NULL) {
      Log.Debug("Expect: %p %s\n", expected, name(expected));
      _expected.push_back(expected);
      expected = va_arg(args, ActionFn);
    }
  }

  virtual void exec(ActionFn listener, const AppState &state, const AppState &oldState, Mode *trigger) {
    Log.Debug("Exec: %p %s\n", listener, name(listener));
    _called.push_back(listener);
  }

  bool check() {
    // TEST_ASSERT_EQUAL(_expected.size(), _called.size());
    if (_expected != _called) {
      Log.Debug("Checking expected: ");
      for (auto pfn = _expected.begin(); pfn!=_expected.end(); ++pfn) {
        Log.Debug_("%s, ", name(*pfn));
      }
      Log.Debug("\n......vs executed: ");
      for (auto pfn = _called.begin(); pfn!=_called.end(); ++pfn) {
        Log.Debug_("%s, ", name(*pfn));
      }
      Log.Debug("\n");
    }
    return _expected == _called;
  }

  const char *name(ActionFn fn) {
    for (uint16_t i=0; i<ELEMENTS(_names); ++i) {
      if (fn==_names[i].fn) {
        return _names[i].name;
      }
    }
    return "unknown";
  }
};

void test_gps_power_while_power(void) {
  AppState state;
  TestClock clock;
  TestExecutor expectedOps(NULL);
  RespireContext<AppState> respire(state, ModeFunctional, &clock, &expectedOps);
  respire.begin();

  {
    TestExecutor expectedOps(NULL);
    respire.setExecutor(&expectedOps);

    TEST_ASSERT_FALSE(state.getUsbPower());
    TEST_ASSERT_FALSE(state.getGpsPower());

    TEST_ASSERT(expectedOps.check());
  }

  {
    TestExecutor expectedOps(changeGpsPower, NULL);
    respire.setExecutor(&expectedOps);

    state.setUsbPower(true);

    TEST_ASSERT(state.getUsbPower());
    TEST_ASSERT(state.getGpsPower());

    TEST_ASSERT(expectedOps.check());
  }

  {
    TestExecutor expectedOps(changeGpsPower, changeSleep, NULL);
    respire.setExecutor(&expectedOps);

    state.setUsbPower(false);

    TEST_ASSERT_FALSE(state.getUsbPower());
    TEST_ASSERT_FALSE(state.getGpsPower());

    TEST_ASSERT(expectedOps.check());
  }
}

void test_join_once_when_low_power_then_sleep_on_fail(void) {
  // When low power and not joined, attempt join once.
  // Don't repeatedly attempt join as time passes.

  TestClock clock;
  TestExecutor expectedOps(attemptJoin, NULL);
  AppState state;
  RespireContext<AppState> respire(state, ModeFunctional, &clock, &expectedOps);
  respire.begin();

  TEST_ASSERT_FALSE(state.getUsbPower());
  TEST_ASSERT_FALSE(state.getJoined());
  TEST_ASSERT_FALSE(state.getGpsPower());

  TEST_ASSERT(expectedOps.check());

  {
    TestExecutor expectedOps(changeSleep, NULL); // Just goes to sleep, does not attempt multiple joins
    respire.setExecutor(&expectedOps);

    for (uint32_t s = 1; s<60; s += 1) {
      clock.advanceSeconds(1);
      respire.loop();
      if (s==2) {
        respire.complete(ModeAttemptJoin);
      }
    }

    TEST_ASSERT_FALSE(ModeAttemptJoin.isActive(state));
    TEST_ASSERT_FALSE(ModeLowPowerJoin.isActive(state));

    TEST_ASSERT(expectedOps.check());
  }
}

void test_gps_power_and_send_after_low_power_successful_join(void) {
  TestClock clock;
  TestExecutor expectedOps(attemptJoin, NULL);
  AppState state;
  RespireContext<AppState> respire(state, ModeFunctional, &clock, &expectedOps);
  respire.begin();

  TEST_ASSERT_FALSE(state.getUsbPower());
  TEST_ASSERT_FALSE(state.getJoined());
  TEST_ASSERT_FALSE(state.getGpsPower());
  TEST_ASSERT(expectedOps.check());

  {
    TestExecutor expectedOps(changeGpsPower, NULL);
    respire.setExecutor(&expectedOps);

    respire.complete(ModeAttemptJoin, [](AppState &state){
      state.setJoined(true);
    });

    TEST_ASSERT(state.getJoined());
    TEST_ASSERT(state.getGpsPower());
    TEST_ASSERT(ModeLowPowerGpsSearch.isActive(state));
    TEST_ASSERT_FALSE(ModeSleep.isActive(state));

    TEST_ASSERT(expectedOps.check());
  }

  {
    TestExecutor expectedOps(changeGpsPower, readGpsLocation, sendLocationAck, writeLocation, changeSleep, NULL);
    respire.setExecutor(&expectedOps);

    state.setGpsFix(true);

    respire.complete(ModeReadGps, [](AppState &state) {
      GpsSample sample(45, 45, 45, 1.5, 2018, 03, 20, 12, 00, 00, 0000);
      state.setGpsLocation(sample);
    });

    TEST_ASSERT(state.getJoined());
    TEST_ASSERT_FALSE(state.getGpsPower());
    TEST_ASSERT_FALSE(ModeLowPowerGpsSearch.isActive(state));
    TEST_ASSERT(ModeSend.isActive(state));
    TEST_ASSERT_FALSE(ModeLogGps.isActive(state));
    TEST_ASSERT_FALSE(ModeSleep.isActive(state));

    respire.complete(ModeSendAck);

    TEST_ASSERT_FALSE(ModeSend.isActive(state));
    TEST_ASSERT(ModeLogGps.isActive(state));
    TEST_ASSERT_FALSE(ModeSleep.isActive(state));

    respire.complete(ModeLogGps);

    TEST_ASSERT(state.getJoined());
    TEST_ASSERT_FALSE(state.getGpsPower());
    TEST_ASSERT_FALSE(ModeLowPowerGpsSearch.isActive(state));
    TEST_ASSERT_FALSE(ModeSend.isActive(state));
    TEST_ASSERT_FALSE(ModeLogGps.isActive(state));
    TEST_ASSERT(ModeSleep.isActive(state));

    TEST_ASSERT(expectedOps.check());
  }
}

void test_5m_limit_on_low_power_gps_search(void) {
  TestClock clock;
  TestExecutor expectedOps(attemptJoin, changeGpsPower, NULL);
  AppState state;
  RespireContext<AppState> respire(state, ModeFunctional, &clock, &expectedOps);
  respire.begin();

  respire.complete(ModeAttemptJoin, [](AppState &state){
    state.setJoined(true);
  });

  TEST_ASSERT_FALSE(state.getUsbPower());
  TEST_ASSERT(state.getJoined());
  TEST_ASSERT_FALSE(state.hasGpsFix());
  TEST_ASSERT(state.getGpsPower());
  TEST_ASSERT(ModeLowPowerGpsSearch.isActive(state));
  TEST_ASSERT(expectedOps.check());

  {
    TestExecutor expectedOps(changeGpsPower, changeSleep, NULL);
    respire.setExecutor(&expectedOps);

    clock.advanceSeconds(60);
    respire.loop();
    TEST_ASSERT(ModeLowPowerGpsSearch.isActive(state));
    TEST_ASSERT_FALSE(ModeSleep.isActive(state));

    clock.advanceSeconds(4 * 60);
    respire.loop();
    TEST_ASSERT_FALSE(ModeLowPowerGpsSearch.isActive(state));
    TEST_ASSERT(ModeSleep.isActive(state));

    TEST_ASSERT(expectedOps.check());
  }
}

void startedJoinAfter(RespireContext<AppState> &respire, const char *context, AppState &state, TestClock &clock, uint16_t seconds, ActionFn expected, ...) {
  // Starting fresh and we attempt a send.
  va_list args;
  va_start(args, expected);
  TestExecutor expectedOps(expected, args);
  va_end(args);
  respire.setExecutor(&expectedOps);

  clock.advanceSeconds(seconds);
  respire.loop();

  TEST_ASSERT_MESSAGE(ModePeriodicJoin.isActive(state), context);
  TEST_ASSERT_FALSE_MESSAGE(ModeSend.isActive(state), context);
  TEST_ASSERT_FALSE_MESSAGE(ModeSendAck.isActive(state) || ModeSendNoAck.isActive(state), context);
  TEST_ASSERT_FALSE_MESSAGE(ModeSleep.isActive(state), context);

  if (ModeAttemptJoin.isActive(state)) {
    respire.complete(ModeAttemptJoin);
  }
  respire.loop();

  TEST_ASSERT_MESSAGE(ModePeriodicJoin.isActive(state), context);
  TEST_ASSERT_FALSE_MESSAGE(ModeSend.isActive(state), context);
  TEST_ASSERT_FALSE_MESSAGE(ModeSleep.isActive(state), context);

  TEST_ASSERT_MESSAGE(expectedOps.check(), context);
}

void test_join_every_5_min(void) {
  TEST_ASSERT_EQUAL_MESSAGE(TimeUnitHour, ModePeriodicJoin._perUnit, "Tests expect 6 sends per *hour*");
  TEST_ASSERT_EQUAL_MESSAGE(12, ModePeriodicJoin._perTimes, "Tests expect *12* sends per hour");

  TestClock clock;
  TestExecutor expectedOps(attemptJoin, changeGpsPower, NULL);
  AppState state;
  RespireContext<AppState> respire(state, ModeFunctional, &clock, &expectedOps);
  respire.begin();

  // Setup our state...
  state.setUsbPower(true);
  respire.complete(ModeAttemptJoin);

  TEST_ASSERT(state.getUsbPower());
  TEST_ASSERT_FALSE(state.getJoined());
  TEST_ASSERT(state.getGpsPower());
  TEST_ASSERT_FALSE(ModeSleep.isActive(state));
  TEST_ASSERT_FALSE(ModeAttemptJoin.isActive(state));
  TEST_ASSERT(ModePeriodicJoin.isActive(state));
  TEST_ASSERT(expectedOps.check());

  startedJoinAfter(respire, "[first pass]", state, clock, 1, NULL);

  {
    // Some time passes and we stay in same state and nothing happens.
    TestExecutor expectedOps(NULL);
    respire.setExecutor(&expectedOps);

    clock.advanceSeconds(4 * 60); // 4 minutes here
    respire.loop();

    TEST_ASSERT(ModePeriodicJoin.isActive(state));
    TEST_ASSERT_FALSE(ModeSend.isActive(state));
    TEST_ASSERT_FALSE(ModeSleep.isActive(state));

    TEST_ASSERT(expectedOps.check());
  }

  // Full period passes and we start another send.
  startedJoinAfter(respire, "[second pass]", state, clock, 1 * 60 /* 1 min more, for total of 5 minutes */, attemptJoin, NULL);
}

void startedSendAfter(RespireContext<AppState> &respire, const char *context, AppState &state, TestClock &clock, uint16_t seconds, ActionFn expected, ...) {
  // Starting fresh and we attempt a send.
  va_list args;
  va_start(args, expected);
  TestExecutor expectedOps(expected, args);
  va_end(args);
  respire.setExecutor(&expectedOps);

  clock.advanceSeconds(seconds);
  respire.loop();

  TEST_ASSERT_MESSAGE(ModeReadGps.isActive(state), context);
  respire.complete(ModeReadGps, [](AppState &state){
      GpsSample sample(45, 45, 45, 1.5, 2018, 03, 20, 12, 00, 00, 0000);
      state.setGpsLocation(sample);
  });

  TEST_ASSERT_MESSAGE(ModePeriodicSend.isActive(state), context);
  TEST_ASSERT_MESSAGE(ModeSend.isActive(state), context);
  TEST_ASSERT_MESSAGE(ModeSendAck.isActive(state) ^ ModeSendNoAck.isActive(state), context);
  TEST_ASSERT_FALSE_MESSAGE(ModeSleep.isActive(state), context);

  if (ModeSendAck.isActive(state)) {
    respire.complete(ModeSendAck);
  }
  else {
    respire.complete(ModeSendNoAck);
  }
  respire.complete(ModeLogGps);
  respire.loop();

  TEST_ASSERT_MESSAGE(ModePeriodicSend.isActive(state), context);
  TEST_ASSERT_FALSE_MESSAGE(ModeSend.isActive(state), context);
  TEST_ASSERT_FALSE_MESSAGE(ModeSleep.isActive(state), context);

  TEST_ASSERT_MESSAGE(expectedOps.check(), context);
}

void test_send_every_10_min(void) {
  TEST_ASSERT_EQUAL_MESSAGE(TimeUnitHour, ModePeriodicSend._perUnit, "Tests expect 6 sends per *hour*");
  TEST_ASSERT_EQUAL_MESSAGE(6, ModePeriodicSend._perTimes, "Tests expect *6* sends per hour");

  TestClock clock;
  TestExecutor expectedOps(attemptJoin, changeGpsPower, readGpsLocation, NULL);
  AppState state;
  RespireContext<AppState> respire(state, ModeFunctional, &clock, &expectedOps);

  respire.begin();

  // Setup our state...
  {
    StateTransaction<AppState> t(respire);
    state.setUsbPower(true);
    respire.complete(ModeAttemptJoin, [](AppState &state){
      state.setJoined(true);
    });
    state.setGpsFix(true);
  }

  TEST_ASSERT(state.getUsbPower());
  TEST_ASSERT(state.getJoined());
  TEST_ASSERT(state.getGpsPower());
  TEST_ASSERT_FALSE(ModeSleep.isActive(state));
  TEST_ASSERT_FALSE(ModeAttemptJoin.isActive(state));
  TEST_ASSERT_FALSE(ModeLowPowerJoin.isActive(state));
  TEST_ASSERT(expectedOps.check());

  startedSendAfter(respire, "[first pass]", state, clock, 1, sendLocationAck, writeLocation, NULL);

  {
    // Some time passes and we stay in same state and nothing happens.
    TestExecutor expectedOps(NULL);
    respire.setExecutor(&expectedOps);

    clock.advanceSeconds(5 * 60); // 5 minutes here
    respire.loop();

    TEST_ASSERT(ModePeriodicSend.isActive(state));
    TEST_ASSERT_FALSE(ModeSend.isActive(state));
    TEST_ASSERT_FALSE(ModeSleep.isActive(state));

    TEST_ASSERT(expectedOps.check());
  }

  // Full period passes and we start another send.
  startedSendAfter(respire, "[second pass]", state, clock, 5 * 60 /* 5 min more, for total of 10 minutes */, readGpsLocation, sendLocation, writeLocation, NULL);
}

void test_display(void) {
  TestClock clock;
  TestExecutor expectedOps(displayStatus, displayParameters, displayErrors, NULL);
  AppState state;
  RespireContext<AppState> respire(state, ModeDisplay, &clock, &expectedOps);

  respire.begin();

  state.page(0);
  respire.complete(&ModeDisplayStatus);
  state.page(1);
  respire.complete(&ModeDisplayParameters);
  state.page(2);
  respire.complete(&ModeDisplayErrors);

  TEST_ASSERT(expectedOps.check());

  {
    TestExecutor expectedOps(displayErrors, displayErrors, displayErrors, NULL);
    respire.setExecutor(&expectedOps);

    for (uint8_t field=1; field<4; ++field) {
      state.field(field);
      respire.complete(&ModeDisplayErrors);
    }

    TEST_ASSERT(expectedOps.check());
  }

  {
    TestExecutor expectedOps(displayBlank, NULL);
    respire.setExecutor(&expectedOps);

    clock.advanceSeconds(61);
    respire.loop();

    TEST_ASSERT(expectedOps.check());
  }
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    LogPrinter printer(printFn);
    Log.Init(LOGLEVEL, printer);

    RUN_TEST(test_gps_power_while_power);
    RUN_TEST(test_join_once_when_low_power_then_sleep_on_fail);
    RUN_TEST(test_gps_power_and_send_after_low_power_successful_join);
    RUN_TEST(test_5m_limit_on_low_power_gps_search);
    RUN_TEST(test_join_every_5_min);
    RUN_TEST(test_send_every_10_min);
    RUN_TEST(test_display);
    UNITY_END();

    return 0;
}

#ifdef MOCK_ACTIONS

uint8_t fieldCountForPage(const AppState &state, uint8_t page) {
  return 0;
}

void changeGpsPower(const AppState &state, const AppState &oldState, Mode *triggeringMode) {
  // Reify GpsPower value
}

void readGpsLocation(const AppState &state, const AppState &oldState, Mode *triggeringMode) {
}

void attemptJoin(const AppState &state, const AppState &oldState, Mode *triggeringMode) {
  // Enter the AttempJoin state, which is to say, call lorawan.join()
}

void changeSleep(const AppState &state, const AppState &oldState, Mode *triggeringMode) {
  // Enter or exit Sleep state
}

void writeLocation(const AppState &state, const AppState &oldState, Mode *triggeringMode) {
}

void sendLocation(const AppState &state, const AppState &oldState, Mode *triggeringMode) {
  // Send location
}

void sendLocationAck(const AppState &state, const AppState &oldState, Mode *triggeringMode) {
  // Send location
}

void displayBlank(const AppState &state, const AppState &oldState, Mode *triggeringMode) {
  Log.Debug("Test displayBlank\n");
}

void displayStatus(const AppState &state, const AppState &oldState, Mode *triggeringMode) {
  Log.Debug("Test displayStatus\n");
}

void displayParameters(const AppState &state, const AppState &oldState, Mode *triggeringMode) {
  Log.Debug("Test displayParameters\n");
}

void displayErrors(const AppState &state, const AppState &oldState, Mode *triggeringMode) {
  Log.Debug("Test displayErrors\n");
}

#endif
#endif
