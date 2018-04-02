#ifndef UNIT_TEST

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
#include <ParameterStore.h>

extern AppState gState;
extern RespireContext<AppState> gRespire;
extern ParameterStore gParameters;

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

  Log.Debug("uiSetup display init()\n");
  gDisplay.init();
  gDisplay.setBatteryVisible(true);
  gDisplay.setBatteryIcon(true);
  gDisplay.setBattery(3.5);

  Log.Debug("uiSetup display splash()\n");
  gDisplay.clearMsgArea();
  gDisplay.renderBattery();
  // gDisplay.setTextSize(2);
  gDisplay.println("Manhattan Mapper!");
  gDisplay.println("The Things Network");
  gDisplay.println("New York!");
  gDisplay.println("Let's Get To Work!");
  gDisplay.display();

  // // below first two number equals stating pixel point,the three numbers next to logoname represent width, height and rotation
  // gDisplay.drawBitmap(0, 0, ttn_glcd_bmp, 128, 64, 1);
  // gDisplay.display();
  Log.Debug("uiSetup finished\n");
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

typedef void (*FormatFn)(char *formatted, const AppState &state);

class Field {
  const char * const _pname;
  const size_t _psize;
  FormatFn _formatter;

  char hexFormat(uint8_t hex) {
    hex &= 0x0F;
    if (hex<10) {
      return '0' + hex;
    }
    else {
      return 'A' + hex - 10;
    }
  }

  void bytesToString(char *s, const uint8_t *bytes, const uint16_t size) {
    for (uint16_t i=0; i<size; ++i) {
      *(s++) = hexFormat(bytes[i] >> 4);
      *(s++) = hexFormat(bytes[i]);
    }
    *(s++) = '\0';
  }

  void bytesValue(char *value) {
    uint8_t bytes[_psize];
    int ret = gParameters.get(_pname, bytes, _psize);
    if (ret==PS_SUCCESS) {
      bytesToString(value, bytes, _psize);
    }
    else {
      strncpy(value, "Failed format", 2*_psize);
      value[2*_psize] = '\0';
    }
  }

  void intValue(char *value) {
    uint32_t ivalue = 0;
    int ret = gParameters.get(_pname, &ivalue);
    if (ret==PS_SUCCESS) {
      sprintf(value, "%lu", ivalue);
    }
    else {
      strcpy(value, "Unknown Net ID");
    }
  }

  public:

  Field(const char * const pname, FormatFn formatter)
  : _pname(pname), _psize(0), _formatter(formatter) {
  }

  Field(const char * const pname, const size_t psize)
  : _pname(pname), _psize(psize), _formatter(NULL) {
  }

  Field(const char * const pname)
  : _pname(pname), _psize(0), _formatter(NULL) {
  }

  void display(const AppState &state) {
    gDisplay.setTextSize(2);
    gDisplay.setCursor(0, 0);

    gDisplay.println(_pname);
    char valueBuf[100];
    if (_formatter!=NULL) {
      _formatter(valueBuf, state);
    }
    else if (0 < _psize) {
      bytesValue(valueBuf);
    }
    else {
      intValue(valueBuf);
    }
    gDisplay.println(valueBuf);
  }
};

Field gStatusFields[] = {
  Field("Power", [](char *value, const AppState &state) {
    if (state.getUsbPower()) {
      strcpy(value, "USB");
    }
    else {
      sprintf(value, "Bat (%0.2fV)", state.batteryVolts());
    }
  }),
  Field("GPS Power", [](char *value, const AppState &state) {
    strcpy(value, state.getGpsPower() ? "Yes" : "No");
  }),
  Field("GPS Fix", [](char *value, const AppState &state) {
    strcpy(value, state.hasGpsFix() ? "Yes" : "No");
  }),
  Field("GPS Date", [](char *value, const AppState &state) {
    const GpsSample &gpsSample = state.gpsSample();
    sprintf(value, "%04d/%02d/%02d", gpsSample._year, gpsSample._month, gpsSample._day);
  }),
  Field("GPS Time", [](char *value, const AppState &state) {
    const GpsSample &gpsSample = state.gpsSample();
    sprintf(value, "%02d:%02d:%02d", gpsSample._hour, gpsSample._minute, gpsSample._seconds);
  }),
  Field("GPS Lt/Ln", [](char *value, const AppState &state) {
    const GpsSample &gpsSample = state.gpsSample();
    sprintf(value, "%3.2f : %3.2f", gpsSample._latitude, gpsSample._longitude);
  }),
  Field("GPS Alt/H", [](char *value, const AppState &state) {
    const GpsSample &gpsSample = state.gpsSample();
    sprintf(value, "%3.2f, %3.2f", gpsSample._altitude, gpsSample._HDOP);
  }),
  Field("TTN Join", [](char *value, const AppState &state) {
    strcpy(value, state.getJoined() ? "Yes" : "No");
  }),
  Field("TTN Up", [](char *value, const AppState &state) {
    sprintf(value, "%d", state.ttnFrameCounter());
  }),
  Field("DEVADDR"),
  Field("NWKSKEY", 16),
  Field("APPSKEY", 16),
  // uint32_t _ttnLastSend;
};

Field gParamFields[] = {
  Field("AppEUI", 8),
  Field("DevEUI", 8),
  Field("NETID"),
};

void displayStatus(const AppState &state, const AppState &oldState, Mode *triggeringMode) {
  Log.Debug("Called displayStatus\n");
  gDisplay.clearDisplay();
  gStatusFields[state.field() % ELEMENTS(gStatusFields)].display(state);
  gDisplay.display();
  gRespire.complete(triggeringMode);
}

void displayParameters(const AppState &state, const AppState &oldState, Mode *triggeringMode) {
  Log.Debug("Called displayParameters\n");
  gDisplay.clearDisplay();
  gParamFields[state.field() % ELEMENTS(gParamFields)].display(state);
  gDisplay.display();
  gRespire.complete(triggeringMode);
}

void displayErrors(const AppState &state, const AppState &oldState, Mode *triggeringMode) {
  Log.Debug("Called displayErrors\n");
  gDisplay.clearDisplay();
  gDisplay.setTextSize(2);
  gDisplay.setCursor(0, 0);
  gDisplay.println("Errors");
  gDisplay.display();
  gRespire.complete(triggeringMode);
}

uint8_t fieldCountForPage(const AppState &state, uint8_t page) {
  switch (page) {
    case 0: return ELEMENTS(gStatusFields);
    case 1: return ELEMENTS(gParamFields);
    case 2: return 1;
    default: return 1;
  }
}

#endif
