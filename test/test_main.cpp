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
  ListenerFn fn;
  const char *name;
} _names[] = {
  FNAME(changeGpsPower),
  FNAME(attemptJoin),
  FNAME(changeSleep),
  FNAME(sendLocation),
  FNAME(sendLocationAck),
};

class TestExecutor : public Executor {
  std::vector<ListenerFn> _expected;
  std::vector<ListenerFn> _called;
  public:
  TestExecutor(ListenerFn expected, ...) {
    va_list args;
    va_start(args, expected);
    while (expected!=NULL) {
      Log.Debug(F("Expect: %p %s\n"), expected, name(expected));
      _expected.push_back(expected);
      expected = va_arg(args, ListenerFn);
    }
    va_end(args);
  }

  TestExecutor(ListenerFn expected, va_list &args) {
    while (expected!=NULL) {
      Log.Debug("Expect: %p %s\n", expected, name(expected));
      _expected.push_back(expected);
      expected = va_arg(args, ListenerFn);
    }
  }

  virtual void exec(ListenerFn listener, const AppState &state, const AppState &oldState, Mode *trigger) {
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

  const char *name(ListenerFn fn) {
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
  state.init();

  {
    TestExecutor expectedOps(NULL);
    state.setExecutor(&expectedOps);

    TEST_ASSERT_FALSE(state.getUsbPower());
    TEST_ASSERT_FALSE(state.getGpsPower());

    TEST_ASSERT(expectedOps.check());
  }

  {
    TestExecutor expectedOps(changeGpsPower, NULL);
    state.setExecutor(&expectedOps);

    state.setUsbPower(true);

    TEST_ASSERT(state.getUsbPower());
    TEST_ASSERT(state.getGpsPower());

    TEST_ASSERT(expectedOps.check());
  }

  {
    TestExecutor expectedOps(changeGpsPower, changeSleep, NULL);
    state.setExecutor(&expectedOps);

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
  AppState state(&clock, &expectedOps);
  state.init();

  TEST_ASSERT_FALSE(state.getUsbPower());
  TEST_ASSERT_FALSE(state.getJoined());
  TEST_ASSERT_FALSE(state.getGpsPower());

  TEST_ASSERT(expectedOps.check());

  {
    TestExecutor expectedOps(changeSleep, NULL); // Just goes to sleep, does not attempt multiple joins
    state.setExecutor(&expectedOps);

    for (uint32_t s = 1; s<60; s += 1) {
      clock.advanceSeconds(1);
      state.loop();
      if (s==2) {
        state.complete(ModeAttemptJoin);
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
  AppState state(&clock, &expectedOps);
  state.init();

  TEST_ASSERT_FALSE(state.getUsbPower());
  TEST_ASSERT_FALSE(state.getJoined());
  TEST_ASSERT_FALSE(state.getGpsPower());
  TEST_ASSERT(expectedOps.check());

  {
    TestExecutor expectedOps(changeGpsPower, NULL);
    state.setExecutor(&expectedOps);

    {
      StateTransaction transaction(state);
      state.complete(ModeAttemptJoin);
      state.setJoined(true);
    }

    TEST_ASSERT(state.getJoined());
    TEST_ASSERT(state.getGpsPower());
    TEST_ASSERT(ModeLowPowerGpsSearch.isActive(state));
    TEST_ASSERT_FALSE(ModeSleep.isActive(state));

    TEST_ASSERT(expectedOps.check());
  }

  {
    TestExecutor expectedOps(changeGpsPower, sendLocationAck, NULL);
    state.setExecutor(&expectedOps);

    state.setGpsFix(true);

    TEST_ASSERT(state.getJoined());
    TEST_ASSERT_FALSE(state.getGpsPower());
    TEST_ASSERT_FALSE(ModeLowPowerGpsSearch.isActive(state));
    TEST_ASSERT(ModeSend.isActive(state));
    TEST_ASSERT_FALSE(ModeSleep.isActive(state));

    TEST_ASSERT(expectedOps.check());
  }

  {
    TestExecutor expectedOps(changeSleep, NULL);
    state.setExecutor(&expectedOps);

    state.complete(ModeSendAck);

    TEST_ASSERT(state.getJoined());
    TEST_ASSERT_FALSE(state.getGpsPower());
    TEST_ASSERT_FALSE(ModeLowPowerGpsSearch.isActive(state));
    TEST_ASSERT_FALSE(ModeSend.isActive(state));
    TEST_ASSERT(ModeSleep.isActive(state));

    TEST_ASSERT(expectedOps.check());
  }
}

void test_5m_limit_on_low_power_gps_search(void) {
  TestClock clock;
  TestExecutor expectedOps(attemptJoin, changeGpsPower, NULL);
  AppState state(&clock, &expectedOps);
  state.init();

  state.setJoined(true);
  state.complete(ModeAttemptJoin);

  TEST_ASSERT_FALSE(state.getUsbPower());
  TEST_ASSERT(state.getJoined());
  TEST_ASSERT_FALSE(state.getGpsFix());
  TEST_ASSERT(state.getGpsPower());
  TEST_ASSERT(ModeLowPowerGpsSearch.isActive(state));
  TEST_ASSERT(expectedOps.check());

  {
    TestExecutor expectedOps(changeGpsPower, changeSleep, NULL);
    state.setExecutor(&expectedOps);

    clock.advanceSeconds(60);
    state.loop();
    TEST_ASSERT(ModeLowPowerGpsSearch.isActive(state));
    TEST_ASSERT_FALSE(ModeSleep.isActive(state));

    clock.advanceSeconds(4 * 60);
    state.loop();
    TEST_ASSERT_FALSE(ModeLowPowerGpsSearch.isActive(state));
    TEST_ASSERT(ModeSleep.isActive(state));

    TEST_ASSERT(expectedOps.check());
  }
}

void startedJoinAfter(const char *context, AppState &state, TestClock &clock, uint16_t seconds, ListenerFn expected, ...) {
  // Starting fresh and we attempt a send.
  va_list args;
  va_start(args, expected);
  TestExecutor expectedOps(expected, args);
  va_end(args);
  state.setExecutor(&expectedOps);

  clock.advanceSeconds(seconds);
  state.loop();

  TEST_ASSERT_MESSAGE(ModePeriodicJoin.isActive(state), context);
  TEST_ASSERT_FALSE_MESSAGE(ModeSend.isActive(state), context);
  TEST_ASSERT_FALSE_MESSAGE(ModeSendAck.isActive(state) || ModeSendNoAck.isActive(state), context);
  TEST_ASSERT_FALSE_MESSAGE(ModeSleep.isActive(state), context);

  if (ModeAttemptJoin.isActive(state)) {
    state.complete(ModeAttemptJoin);
  }
  state.loop();

  TEST_ASSERT_MESSAGE(ModePeriodicJoin.isActive(state), context);
  TEST_ASSERT_FALSE_MESSAGE(ModeSend.isActive(state), context);
  TEST_ASSERT_FALSE_MESSAGE(ModeSleep.isActive(state), context);

  TEST_ASSERT_MESSAGE(expectedOps.check(), context);
}

void test_join_every_5_min(void) {
  TestClock clock;
  TestExecutor expectedOps(attemptJoin, changeGpsPower, NULL);
  AppState state(&clock, &expectedOps);
  state.init();

  // Setup our state...
  {
    StateTransaction t(state);
    state.setUsbPower(true);
    state.complete(ModeAttemptJoin);
  }

  TEST_ASSERT(state.getUsbPower());
  TEST_ASSERT_FALSE(state.getJoined());
  TEST_ASSERT(state.getGpsPower());
  TEST_ASSERT_FALSE(ModeSleep.isActive(state));
  TEST_ASSERT_FALSE(ModeAttemptJoin.isActive(state));
  TEST_ASSERT(ModePeriodicJoin.isActive(state));
  TEST_ASSERT(expectedOps.check());

  startedJoinAfter("[first pass]", state, clock, 1, NULL);

  {
    // Some time passes and we stay in same state and nothing happens.
    TestExecutor expectedOps(NULL);
    state.setExecutor(&expectedOps);

    clock.advanceSeconds(4 * 60); // 4 minutes here
    state.loop();

    TEST_ASSERT(ModePeriodicJoin.isActive(state));
    TEST_ASSERT_FALSE(ModeSend.isActive(state));
    TEST_ASSERT_FALSE(ModeSleep.isActive(state));

    TEST_ASSERT(expectedOps.check());
  }

  // Full period passes and we start another send.
  startedJoinAfter("[second pass]", state, clock, 1 * 60 /* 1 min more, for total of 5 minutes */, attemptJoin, NULL);
}

void startedSendAfter(const char *context, AppState &state, TestClock &clock, uint16_t seconds, ListenerFn expected, ...) {
  // Starting fresh and we attempt a send.
  va_list args;
  va_start(args, expected);
  TestExecutor expectedOps(expected, args);
  va_end(args);
  state.setExecutor(&expectedOps);

  clock.advanceSeconds(seconds);
  state.loop();

  TEST_ASSERT_MESSAGE(ModePeriodicSend.isActive(state), context);
  TEST_ASSERT_MESSAGE(ModeSend.isActive(state), context);
  TEST_ASSERT_MESSAGE(ModeSendAck.isActive(state) ^ ModeSendNoAck.isActive(state), context);
  TEST_ASSERT_FALSE_MESSAGE(ModeSleep.isActive(state), context);

  if (ModeSendAck.isActive(state)) {
    state.complete(ModeSendAck);
  }
  else {
    state.complete(ModeSendNoAck);
  }
  state.loop();

  TEST_ASSERT_MESSAGE(ModePeriodicSend.isActive(state), context);
  TEST_ASSERT_FALSE_MESSAGE(ModeSend.isActive(state), context);
  TEST_ASSERT_FALSE_MESSAGE(ModeSleep.isActive(state), context);

  TEST_ASSERT_MESSAGE(expectedOps.check(), context);
}

void test_send_every_10_min(void) {
  TestClock clock;
  TestExecutor expectedOps(attemptJoin, changeGpsPower, sendLocationAck, NULL);
  AppState state(&clock, &expectedOps);
  state.init();

  // Setup our state...
  {
    StateTransaction t(state);
    state.setUsbPower(true);
    state.complete(ModeAttemptJoin);
    state.setJoined(true);
  }

  TEST_ASSERT(state.getUsbPower());
  TEST_ASSERT(state.getJoined());
  TEST_ASSERT(state.getGpsPower());
  TEST_ASSERT_FALSE(ModeSleep.isActive(state));
  TEST_ASSERT_FALSE(ModeAttemptJoin.isActive(state));
  TEST_ASSERT_FALSE(ModeLowPowerJoin.isActive(state));
  TEST_ASSERT(expectedOps.check());

  startedSendAfter("[first pass]", state, clock, 1, NULL);

  {
    // Some time passes and we stay in same state and nothing happens.
    TestExecutor expectedOps(NULL);
    state.setExecutor(&expectedOps);

    clock.advanceSeconds(5 * 60); // 5 minutes here
    state.loop();

    TEST_ASSERT(ModePeriodicSend.isActive(state));
    TEST_ASSERT_FALSE(ModeSend.isActive(state));
    TEST_ASSERT_FALSE(ModeSleep.isActive(state));

    TEST_ASSERT(expectedOps.check());
  }

  // Full period passes and we start another send.
  startedSendAfter("[second pass]", state, clock, 5 * 60 /* 5 min more, for total of 10 minutes */, sendLocation, NULL);
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
    UNITY_END();

    return 0;
}

#ifdef MOCK_ACTIONS

void changeGpsPower(const AppState &state, const AppState &oldState) {
  // Reify GpsPower value
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

void sendLocationAck(const AppState &state, const AppState &oldState) {
  // Send location
}

#endif
#endif
