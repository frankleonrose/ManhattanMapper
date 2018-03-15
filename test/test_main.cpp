#include <Arduino.h>
#include <unity.h>
#include <vector>

#include "mm_state.h"

#define UNIT_TEST
#ifdef UNIT_TEST

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

class TestExecutor : public Executor {
  std::vector<ListenerFn> _expected;
  std::vector<ListenerFn> _called;
  public:
  TestExecutor(ListenerFn expected, ...) {
    va_list args;
    va_start(args, expected);
    while (expected!=NULL) {
      // printf("Expect: %p\n", expected);
      _expected.push_back(expected);
      expected = va_arg(args, ListenerFn);
    }
    va_end(args);
  }

  TestExecutor(ListenerFn expected, va_list &args) {
    while (expected!=NULL) {
      // printf("Expect: %p\n", expected);
      _expected.push_back(expected);
      expected = va_arg(args, ListenerFn);
    }
  }

  virtual void exec(ListenerFn listener, const AppState &state, const AppState &oldState, Mode *trigger) {
    // printf("Exec: %p\n", listener);
    _called.push_back(listener);
  }

  bool check() {
    // TEST_ASSERT_EQUAL(_expected.size(), _called.size());
    return _expected == _called;
  }
};

void test_gps_power_while_power(void) {
  AppState state;
  state.init();

  {
    TestExecutor expectedOps(NULL);
    state.setExecutor(&expectedOps);

    TEST_ASSERT_EQUAL(false, state.getUsbPower());
    TEST_ASSERT_EQUAL(false, state.getGpsPower());

    TEST_ASSERT(expectedOps.check());
  }

  {
    TestExecutor expectedOps(changeGpsPower, changeSleep, NULL);
    state.setExecutor(&expectedOps);

    state.setUsbPower(true);

    TEST_ASSERT_EQUAL(true, state.getUsbPower());
    TEST_ASSERT_EQUAL(true, state.getGpsPower());

    TEST_ASSERT(expectedOps.check());
  }

  {
    TestExecutor expectedOps(changeGpsPower, NULL);
    state.setExecutor(&expectedOps);

    state.setUsbPower(false);

    TEST_ASSERT_EQUAL(false, state.getUsbPower());
    TEST_ASSERT_EQUAL(false, state.getGpsPower());

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

  TEST_ASSERT_EQUAL(false, state.getUsbPower());
  TEST_ASSERT_EQUAL(false, state.getJoined());
  TEST_ASSERT_EQUAL(false, state.getGpsPower());

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

    TEST_ASSERT_EQUAL(false, ModeAttemptJoin.isActive(state));
    TEST_ASSERT_EQUAL(false, ModeLowPowerJoin.isActive(state));

    TEST_ASSERT(expectedOps.check());
  }
}

void test_gps_power_after_low_power_successful_join(void) {
  TestClock clock;
  TestExecutor expectedOps(attemptJoin, NULL);
  AppState state(&clock, &expectedOps);
  state.init();

  TEST_ASSERT_EQUAL(false, state.getUsbPower());
  TEST_ASSERT_EQUAL(false, state.getJoined());
  TEST_ASSERT_EQUAL(false, state.getGpsPower());
  TEST_ASSERT(expectedOps.check());

  {
    TestExecutor expectedOps(changeGpsPower, NULL);
    state.setExecutor(&expectedOps);

    {
      StateTransaction transaction(state);
      state.complete(ModeAttemptJoin);
      state.setJoined(true);
    }

    TEST_ASSERT_EQUAL(true, state.getJoined());
    TEST_ASSERT_EQUAL(true, state.getGpsPower());
    TEST_ASSERT_EQUAL(true, ModeLowPowerGpsSearch.isActive(state));
    TEST_ASSERT_EQUAL(false, ModeSleep.isActive(state));

    TEST_ASSERT(expectedOps.check());
  }
}

void test_5m_limit_on_gps_search(void) {
  TestClock clock;
  TestExecutor expectedOps(attemptJoin, changeGpsPower, NULL);
  AppState state(&clock, &expectedOps);
  state.init();

  state.setJoined(true);
  state.complete(ModeAttemptJoin);

  TEST_ASSERT_EQUAL(false, state.getUsbPower());
  TEST_ASSERT_EQUAL(true, state.getJoined());
  TEST_ASSERT_EQUAL(true, state.getGpsPower());
  TEST_ASSERT_EQUAL(true, ModeLowPowerGpsSearch.isActive(state));
  TEST_ASSERT(expectedOps.check());

  {
    TestExecutor expectedOps(changeGpsPower, changeSleep, NULL);
    state.setExecutor(&expectedOps);

    clock.advanceSeconds(60);
    state.loop();
    TEST_ASSERT_EQUAL(true, ModeLowPowerGpsSearch.isActive(state));
    TEST_ASSERT_EQUAL(false, ModeSleep.isActive(state));

    clock.advanceSeconds(4 * 60);
    state.loop();
    TEST_ASSERT_EQUAL(false, ModeLowPowerGpsSearch.isActive(state));
    TEST_ASSERT_EQUAL(true, ModeSleep.isActive(state));

    TEST_ASSERT(expectedOps.check());
  }
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

  TEST_ASSERT_EQUAL_MESSAGE(true, ModePeriodicSend.isActive(state), context);
  TEST_ASSERT_EQUAL_MESSAGE(true, ModeSend.isActive(state), context);
  TEST_ASSERT_EQUAL_MESSAGE(false, ModeSleep.isActive(state), context);

  state.complete(ModeSend);
  state.loop();

  TEST_ASSERT_EQUAL_MESSAGE(true, ModePeriodicSend.isActive(state), context);
  TEST_ASSERT_EQUAL_MESSAGE(false, ModeSend.isActive(state), context);
  TEST_ASSERT_EQUAL_MESSAGE(false, ModeSleep.isActive(state), context);

  TEST_ASSERT_MESSAGE(expectedOps.check(), context);
}

void test_send_every_10_min(void) {
  TestClock clock;
  TestExecutor expectedOps(attemptJoin, changeGpsPower, sendLocation, NULL);
  AppState state(&clock, &expectedOps);
  state.init();

  // Setup our state...
  {
    StateTransaction t(state);
    state.setUsbPower(true);
    state.setJoined(true);
  }

  TEST_ASSERT_EQUAL(true, state.getUsbPower());
  TEST_ASSERT_EQUAL(true, state.getJoined());
  TEST_ASSERT_EQUAL(true, state.getGpsPower());
  TEST_ASSERT_EQUAL(false, ModeSleep.isActive(state));
  TEST_ASSERT_EQUAL(false, ModeAttemptJoin.isActive(state));
  TEST_ASSERT_EQUAL(false, ModeLowPowerJoin.isActive(state));
  TEST_ASSERT(expectedOps.check());

  startedSendAfter("[first pass]", state, clock, 1, NULL);

  {
    // Some time passes and we stay in same state and nothing happens.
    TestExecutor expectedOps(NULL);
    state.setExecutor(&expectedOps);

    clock.advanceSeconds(5 * 60);
    state.loop();

    TEST_ASSERT_EQUAL(true, ModePeriodicSend.isActive(state));
    TEST_ASSERT_EQUAL(false, ModeSend.isActive(state));
    TEST_ASSERT_EQUAL(false, ModeSleep.isActive(state));

    TEST_ASSERT(expectedOps.check());
  }

  // Full period passes and we start another send.
  startedSendAfter("[second pass]", state, clock, 5 * 60, sendLocation, NULL);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_gps_power_while_power);
    RUN_TEST(test_join_once_when_low_power_then_sleep_on_fail);
    RUN_TEST(test_gps_power_after_low_power_successful_join);
    RUN_TEST(test_5m_limit_on_gps_search);
    RUN_TEST(test_send_every_10_min);
    UNITY_END();

    return 0;
}

#endif
