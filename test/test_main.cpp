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

  virtual void exec(ListenerFn listener, AppState state, AppState oldState) {
    _called.push_back(listener);
  }

  bool check() {
    return _expected == _called;
  }
};

void test_gps_power_while_power(void) {
  AppState state;
  state.init();

  {
    TestExecutor expectedOps(NULL);
    state.setExecutor(&expectedOps);

    TEST_ASSERT_EQUAL(state.getUsbPower(), false);
    TEST_ASSERT_EQUAL(state.getGpsPower(), false);

    TEST_ASSERT(expectedOps.check());
  }

  {
    TestExecutor expectedOps(onChangeGpsPower, NULL);
    state.setExecutor(&expectedOps);

    state.setUsbPower(true);

    TEST_ASSERT_EQUAL(state.getUsbPower(), true);
    TEST_ASSERT_EQUAL(state.getGpsPower(), true);

    TEST_ASSERT(expectedOps.check());
  }

  {
    TestExecutor expectedOps(onChangeGpsPower, NULL);
    state.setExecutor(&expectedOps);

    state.setUsbPower(false);

    TEST_ASSERT_EQUAL(state.getUsbPower(), false);
    TEST_ASSERT_EQUAL(state.getGpsPower(), false);

    TEST_ASSERT(expectedOps.check());
  }
}

void test_join_once_when_low_power(void) {
  // When low power and not joined, attempt join once.
  // Don't repeatedly attempt join as time passes.

  TestExecutor expectedOps(onAttemptJoin, NULL);
  AppState state(&expectedOps);
  state.init();

  TEST_ASSERT_EQUAL(state.getUsbPower(), false);
  TEST_ASSERT_EQUAL(state.getJoined(), false);
  TEST_ASSERT_EQUAL(state.getGpsPower(), false);

  TEST_ASSERT(expectedOps.check());
}

void test_led_state_low(void) {
    digitalWrite(LED_BUILTIN, LOW);
    TEST_ASSERT_EQUAL(digitalRead(LED_BUILTIN), LOW);
}

// void setup() {
//     // NOTE!!! Wait for >2 secs
//     // if board doesn't support software reset via Serial.DTR/RTS
//     delay(2000);

//     UNITY_BEGIN();    // IMPORTANT LINE!
//     RUN_TEST(test_gps_power_while_power);

//     pinMode(LED_BUILTIN, OUTPUT);
// }

// uint8_t i = 0;
// uint8_t max_blinks = 5;

// void loop() {
//     if (i < max_blinks)
//     {
//         RUN_TEST(test_join_once_when_low_power);
//         delay(500);
//         RUN_TEST(test_led_state_low);
//         delay(500);
//         i++;
//     }
//     else if (i == max_blinks) {
//       UNITY_END(); // stop unit testing
//     }
// }

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
