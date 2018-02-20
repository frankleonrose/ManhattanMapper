#include <Arduino.h>
#include <unity.h>
#include <vector>

#include "mm_state.h"

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
      _expected.push_back(expected);
      expected = va_arg(args, ListenerFn);
    }
    va_end(args);
  }

  virtual void exec(ListenerFn listener, const AppState &state, const AppState &oldState, Mode *trigger) {
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
    TestExecutor expectedOps(onChangeGpsPower, NULL);
    state.setExecutor(&expectedOps);

    state.setUsbPower(true);

    TEST_ASSERT_EQUAL(true, state.getUsbPower());
    TEST_ASSERT_EQUAL(true, state.getGpsPower());

    TEST_ASSERT(expectedOps.check());
  }

  {
    TestExecutor expectedOps(onChangeGpsPower, NULL);
    state.setExecutor(&expectedOps);

    state.setUsbPower(false);

    TEST_ASSERT_EQUAL(false, state.getUsbPower());
    TEST_ASSERT_EQUAL(false, state.getGpsPower());

    TEST_ASSERT(expectedOps.check());
  }
}

void test_join_once_when_low_power(void) {
  // When low power and not joined, attempt join once.
  // Don't repeatedly attempt join as time passes.

  TestClock clock;
  TestExecutor expectedOps(onAttemptJoin, NULL);
  AppState state(&clock, &expectedOps);
  state.init();

  TEST_ASSERT_EQUAL(false, state.getUsbPower());
  TEST_ASSERT_EQUAL(false, state.getJoined());
  TEST_ASSERT_EQUAL(false, state.getGpsPower());

  TEST_ASSERT(expectedOps.check());

  {
    TestExecutor expectedOps(NULL);
    state.setExecutor(&expectedOps);

    for (uint32_t s = 1; s<60; s += 1) {
      clock.advanceSeconds(1);
      state.loop();
      if (s==2) {
        state.complete(&state.getModeAttemptJoin(), state.getModeAttemptJoin().getStartIndex());
      }
    }

    TEST_ASSERT_EQUAL(false, state.getModeAttemptJoin().getActive());
    TEST_ASSERT_EQUAL(false, state.getModeLowPowerJoin().getActive());

    TEST_ASSERT(expectedOps.check());
  }
}

void test_led_state_low(void) {
    digitalWrite(LED_BUILTIN, LOW);
    TEST_ASSERT_EQUAL(digitalRead(LED_BUILTIN), LOW);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_gps_power_while_power);
    RUN_TEST(test_join_once_when_low_power);
    RUN_TEST(test_led_state_low);
    // RUN_TEST(test_function_calculator_division);
    UNITY_END();

    return 0;
}

#endif