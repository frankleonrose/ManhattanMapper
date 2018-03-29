#include <Arduino.h>
#include <avdweb_SAMDtimer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_FeatherOLED.h>
#include <mm_state.h>

extern AppState gState;
extern RespireContext<AppState> gRespire;

Adafruit_FeatherOLED gDisplay;

#define BUTTON_TIMER_PERIOD_MICROSECONDS 2000
#define BUTTON_A_PIN 6
#define BUTTON_B_PIN 6
#define BUTTON_C_PIN 6

static volatile bool gButtonA = false;
static volatile bool gButtonB = false;
static volatile bool gButtonC = false;

#define DEBOUNCE_MASK 0xE000

void ISR_TC3_readButtons(struct tc_module *const module_inst) {
  // Thanks, http://www.eng.utah.edu/~cs5780/debouncing.pdf "A Guide to Debouncing" by Jack Ganssle section: "An Alternative"
  // Basically, gButtonX is set only after a quiet stream of 25 LOW values have come in. At 2ms sample rate that allows ~50ms detect time.
  static uint32_t debounceA = 0, debounceB = 0, debounceC = 0;
  debounceA = (debounceA << 1) | digitalRead(BUTTON_A_PIN) | DEBOUNCE_MASK;
  gButtonA = debounceA == DEBOUNCE_MASK;
  debounceB = (debounceB << 1) | digitalRead(BUTTON_B_PIN) | DEBOUNCE_MASK;
  gButtonB = debounceB == DEBOUNCE_MASK;
  debounceC = (debounceC << 1) | digitalRead(BUTTON_C_PIN) | DEBOUNCE_MASK;
  gButtonC = debounceC == DEBOUNCE_MASK;
}

SAMDtimer gButtonTimer = SAMDtimer(3, ISR_TC3_readButtons, BUTTON_TIMER_PERIOD_MICROSECONDS, false);

void uiSetup() {
  pinMode(BUTTON_A_PIN, INPUT_PULLUP);
  pinMode(BUTTON_B_PIN, INPUT_PULLUP);
  pinMode(BUTTON_C_PIN, INPUT_PULLUP);

  gButtonTimer.enable(true);

  gDisplay.init();
  gDisplay.setBatteryVisible(true);
  gDisplay.setBatteryIcon(true);
  gDisplay.setBattery(3.5);

  gDisplay.clearMsgArea();
  gDisplay.renderBattery();
  // gDisplay.setTextSize(2);
  gDisplay.println("Manhattan Mapper!");
  gDisplay.println("The Things Network");
  gDisplay.println("New York!");
  gDisplay.println("Let's Get To Work!");
  gDisplay.display();

  // gDisplay.clearDisplay();
  // // below first two number equals stating pixel point,the three numbers next to logoname represent width, height and rotation
  // gDisplay.drawBitmap(0, 0, ttn_glcd_bmp, 128, 64, 1);
  // gDisplay.display();
}

void uiLoop() {
  gState.buttonPage(gButtonA);
  gState.buttonField(gButtonB);
  gState.buttonChange(gButtonC);
}

void displayBlank(const AppState &state, const AppState &oldState, Mode *triggeringMode) {
  Log.Debug("displayBlank\n");
  gDisplay.clearDisplay();
  gDisplay.display();
}

void displayStatus(const AppState &state, const AppState &oldState, Mode *triggeringMode) {
  Log.Debug("displayStatus\n");
  gDisplay.clearMsgArea();
  gDisplay.renderBattery();
  gDisplay.println("Status");
  gDisplay.display();
  gRespire.complete(triggeringMode);
}

void displayParameters(const AppState &state, const AppState &oldState, Mode *triggeringMode) {
  Log.Debug("displayParameters\n");
  gDisplay.clearMsgArea();
  gDisplay.renderBattery();
  gDisplay.println("Parameters");
  gDisplay.display();
  gRespire.complete(triggeringMode);
}

void displayErrors(const AppState &state, const AppState &oldState, Mode *triggeringMode) {
  Log.Debug("displayErrors\n");
  gDisplay.clearMsgArea();
  gDisplay.renderBattery();
  gDisplay.println("Errors");
  gDisplay.display();
  gRespire.complete(triggeringMode);
}
