#include <Arduino.h>
#include <Adafruit_ZeroTimer.h>
// Simplifying wrapper lib didn't work for me.
// TC_WAVE_GENERATION_MATCH_PWM vs my TC_WAVE_GENERATION_MATCH_FREQ and callback channel 1 vs my channel 0.
// But I still like that he has logic to figure out scaling given desired freq/period.
// #include <avdweb_SAMDtimer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_FeatherOLED.h>
#include <mm_state.h>

extern AppState gState;
extern RespireContext<AppState> gRespire;

Adafruit_FeatherOLED gDisplay;

#define BUTTON_TIMER_PERIOD_MICROSECONDS 2000
#define BUTTON_A_PIN 9
#define BUTTON_B_PIN 6
#define BUTTON_C_PIN 5

static volatile bool gButtonA = false;
static volatile bool gButtonB = false;
static volatile bool gButtonC = false;

#define DEBOUNCE_MASK 0xE0000000
static volatile bool ignoreA = false;

void ISR_TC3_readButtons(struct tc_module *const module_inst) {
  // static bool ledFlash = false;
  // digitalWrite(LED_BUILTIN, (ledFlash = !ledFlash));

  // Thanks, http://www.eng.utah.edu/~cs5780/debouncing.pdf "A Guide to Debouncing" by Jack Ganssle section: "An Alternative"
  // Basically, gButtonX is set only after a quiet stream of 25 LOW values have come in. At 2ms sample rate that allows ~50ms detect time.
  static uint32_t debounceA = 0, debounceB = 0, debounceC = 0;
  if (!ignoreA) {
    debounceA = (debounceA << 1) | digitalRead(BUTTON_A_PIN) | DEBOUNCE_MASK;
    gButtonA = debounceA == DEBOUNCE_MASK;
  }
  debounceB = (debounceB << 1) | digitalRead(BUTTON_B_PIN) | DEBOUNCE_MASK;
  gButtonB = debounceB == DEBOUNCE_MASK;
  debounceC = (debounceC << 1) | digitalRead(BUTTON_C_PIN) | DEBOUNCE_MASK;
  gButtonC = debounceC == DEBOUNCE_MASK;
}

static Adafruit_ZeroTimer uiButtonTimer = Adafruit_ZeroTimer(3);

float uiReadSharedVbatPin(int pin) {
  // Shared bin with OLED button A. Gotta read and then give it back.
  assert(pin==BUTTON_A_PIN);

  // uiButtonTimer.enable(false);
  ignoreA = true;

  pinMode(pin, INPUT);
  delayMicroseconds(500); // Wait analog signal to settle.

  float val = analogRead(pin);

  pinMode(pin, INPUT_PULLUP);
  // uiButtonTimer.enable(true);
  ignoreA = false;

  return val;
}
void uiSetup() {
  pinMode(BUTTON_A_PIN, INPUT_PULLUP);
  pinMode(BUTTON_B_PIN, INPUT_PULLUP);
  pinMode(BUTTON_C_PIN, INPUT_PULLUP);

  Log.Debug("uiSetup setup button timer\n");

  uiButtonTimer.configure(TC_CLOCK_PRESCALER_DIV4, // prescaler: 48000kHz(m0 clock freq) / 64(prescaler) = 750kHz
                TC_COUNTER_SIZE_16BIT,            // bit width of timer/counter (avoid 32 bit because that uses two timers!)
                TC_WAVE_GENERATION_MATCH_FREQ     // match style
                );

  uiButtonTimer.setPeriodMatch(1500, 1, 0); // 500Hz = 750kHz / 1500, 1 match, 0 channel
  uiButtonTimer.setCallback(true, TC_CALLBACK_CC_CHANNEL0, ISR_TC3_readButtons);
  Log.Debug("uiSetup enable button timer\n");
  uiButtonTimer.enable(true);

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
  // static uint16_t occasion = 0;
  // if ((occasion++ % 1000)==0) {
  //   Log.Debug("Debounce: %x, %x, %x\n", debounceA, debounceB, debounceC);
  // }
  gState.buttonPage(gButtonA);
  gState.buttonField(gButtonB);
  gState.buttonChange(gButtonC);
}

void displayBlank(const AppState &state, const AppState &oldState, Mode *triggeringMode) {
  Log.Debug("Called displayBlank\n");
  gDisplay.clearDisplay();
  gDisplay.display();
  gRespire.complete(triggeringMode);
}

void displayStatus(const AppState &state, const AppState &oldState, Mode *triggeringMode) {
  Log.Debug("Called displayStatus\n");
  gDisplay.clearMsgArea();
  gDisplay.renderBattery();
  gDisplay.println("Status");
  gDisplay.display();
  gRespire.complete(triggeringMode);
}

void displayParameters(const AppState &state, const AppState &oldState, Mode *triggeringMode) {
  Log.Debug("Called displayParameters\n");
  gDisplay.clearMsgArea();
  gDisplay.renderBattery();
  gDisplay.println("Parameters");
  gDisplay.display();
  gRespire.complete(triggeringMode);
}

void displayErrors(const AppState &state, const AppState &oldState, Mode *triggeringMode) {
  Log.Debug("Called displayErrors\n");
  gDisplay.clearMsgArea();
  gDisplay.renderBattery();
  gDisplay.println("Errors");
  gDisplay.display();
  gRespire.complete(triggeringMode);
}
