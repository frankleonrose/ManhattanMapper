/*******************************************************************************
 * Copyright (c) 2018 Frank Leon Rose
 *
 * Permission is hereby granted, free of charge, to anyone
 * obtaining a copy of this document and accompanying files,
 * to do whatever they want with them without any restriction,
 * including, but not limited to, copying, modification and redistribution.
 * NO WARRANTY OF ANY KIND IS PROVIDED.
 *
 * ManhattanMapper built with
 * - Feather LoRa M0
 * - Adafruit GPS Featherwing
 * - Adafruit SD Data Logger Featherwing
 * - Adafruit OLED Featherwing
 *
 * Specification
 * - Adjust send to request ACK once per day                                                [test_send_every_10_min]
 * - When powered, power on GPS                                                             [test_gps_power_while_power]
 * - While powered and gpsfix, store location every 1 minutes
 * - While powered and not joined, attempt join every 5 minutes                             [test_join_every_5_min]
 * - While powered and joined and gpsfix, send location every 10 minutes                    [test_send_every_10_min]
 * - When powered and Δack-received-count and (frame counter > 10,000), attempt rejoin
 * - When low power and nothing happening, sleep                                            [test_join_once_when_low_power_then_sleep_on_fail]
 * - When low power and not joined, attempt join once                                       [test_join_once_when_low_power_then_sleep_on_fail]
 * - When low power and joined, power on GPS                                                [test_gps_power_and_send_after_low_power_successful_join]
 * - When low power and gpsfix, send once                                                   [test_gps_power_and_send_after_low_power_successful_join]
 * - When low power and gpsfix, store location once                                         TODO LowPowerFix terminates when BOTH send & store complete
 * - When low power and joined and sent once, sleep                                         [test_gps_power_and_send_after_low_power_successful_join]
 * - When low power and joined and waketime > 5m, sleep (still awake because no gpsfix)     [test_5m_limit_on_low_power_gps_search]
 * - When low powered and Δack-received-count and (frame counter > 14,000), attempt rejoin
 *
 *******************************************************************************/

#ifndef UNIT_TEST

#include <SPI.h>

#include <Adafruit_GPS.h>
#include <Arduino_LoRaWAN_ttn.h>
#include <RTClib.h>
#include <Logging.h>
#include <ParameterStore.h>
#include <RamStore.h>
#include <LoraStack.h>
#include <Timer.h>

#include "mm_state.h"
#include "gps.h"
#include "storage.h"
#include "ui.h"

// #define LOGLEVEL LOG_LEVEL_DEBUG //  _NOOUTPUT, _ERRORS, _WARNINGS, _INFOS, _DEBUG, _VERBOSE

#if !defined(MIN)
#define MIN(a,b) ((a)<(b) ? (a) : (b))
#endif
#if !defined(MAX)
#define MAX(a,b) ((a)>(b) ? (a) : (b))
#endif

RTC_PCF8523 gRTC;
Clock gClock;
Executor<AppState> gExecutor;
AppState gState;
RespireContext<AppState> gRespire(gState, ModeMain, &gClock, &gExecutor);

Timer gTimer;

// Lorawan Device ID, App ID, and App Key
const char *devEui = "006158A2D06A7A4E";
const char *appEui = "70B3D57EF0001C38";
const char *appKey = "4681950BEFE343C33BD9BB81CA68A89E";

#define PACKET_FORMAT_ID 0x05

static constexpr uint8_t LMIC_UNUSED_PIN = 0xff;

// Pin mapping
#define VBATPIN A7
#define VUSBPIN A1

#define LORA_CS 8
const Arduino_LoRaWAN::lmic_pinmap define_lmic_pins = {
// Feather LoRa wiring (with IO1 <--> GPIO#6)
    .nss = LORA_CS,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = 4,
    .dio = {3, 17, LMIC_UNUSED_PIN},
};

const int STORE_SIZE = 2000;
RamStore<STORE_SIZE> byteStore;
ParameterStore gParameters(byteStore);
LoraStack_LoRaWAN lorawan(define_lmic_pins, gParameters);
LoraStack node(lorawan, gParameters, TTN_FP_US915);

void onEvent(void *ctx, uint32_t event) {
  if (event==EV_TXCOMPLETE) {
    Log.Debug(F("EV_TXCOMPLETE (includes waiting for RX windows)" CR));
    if (LMIC.txrxFlags & TXRX_ACK) {
      Log.Debug(F("Received ack" CR));
      gRespire.complete(ModeSendAck, [](AppState &state) {
        state.transmittedFrame(LMIC.seqnoUp);
      });
    }
    else {
      Log.Debug(F("Did not receive ack" CR));
      gRespire.complete(ModeSendNoAck, [](AppState &state) {
        state.transmittedFrame(LMIC.seqnoUp);
      });
    }
    writeParametersToSD(gParameters);
    digitalWrite(LED_BUILTIN, LOW);
  }
  else {
    switch(event) {
        case EV_SCAN_TIMEOUT:
            Log.Debug(F("EV_SCAN_TIMEOUT" CR));
            break;
        case EV_BEACON_FOUND:
            Log.Debug(F("EV_BEACON_FOUND" CR));
            break;
        case EV_BEACON_MISSED:
            Log.Debug(F("EV_BEACON_MISSED" CR));
            break;
        case EV_BEACON_TRACKED:
            Log.Debug(F("EV_BEACON_TRACKED" CR));
            break;
        case EV_JOINING:
            Log.Debug(F("EV_JOINING" CR));
            break;
        case EV_JOINED:
            Log.Debug(F("EV_JOINED" CR));
            Log.Debug(F("Writing parameters to SD card\n"));
            writeParametersToSD(gParameters);
            gRespire.complete(ModeAttemptJoin, [](AppState &state){
              state.setJoined(true);
              state.transmittedFrame(LMIC.seqnoUp);
            });
            break;
        case EV_RFU1:
            Log.Debug(F("EV_RFU1" CR));
            break;
        case EV_JOIN_FAILED:
            Log.Debug(F("EV_JOIN_FAILED" CR));
            LMIC_reset(); // Otherwise MCCI Arduino LoRaWAN library keeps trying to join.
            gRespire.complete(ModeAttemptJoin); // Don't call setJoin(false) - we may be attempting rejoin, in which case old keys still valid
            break;
        case EV_REJOIN_FAILED:
            Log.Debug(F("EV_REJOIN_FAILED" CR));
            break;
        case EV_LOST_TSYNC:
            Log.Debug(F("EV_LOST_TSYNC" CR));
            break;
        case EV_RESET:
            Log.Debug(F("EV_RESET" CR));
            break;
        case EV_RXCOMPLETE:
            // data received in ping slot
            Log.Debug(F("EV_RXCOMPLETE" CR));
            break;
        case EV_LINK_DEAD:
            Log.Debug(F("EV_LINK_DEAD" CR));
            break;
        case EV_LINK_ALIVE:
            Log.Debug(F("EV_LINK_ALIVE" CR));
            break;
        case EV_SCAN_FOUND:
            Log.Debug(F("EV_SCAN_FOUND" CR));
            break;
        case EV_TXSTART:
            Log.Debug(F("EV_TXSTART" CR));
            break;
        default:
            Log.Debug(F("Unknown Event: %d" CR), event);
            break;
    }
  }
}

void onReceive(const uint8_t *payload, size_t size, port_t port) {
  Log.Debug(F("Received message on port: %d" CR), port);
  Log.Debug(F("Message [%d]: %*m" CR), size, size, payload);
}

static float measuredToVoltage(float measured) {
  measured *= 2;    // we divided by 2 with resistors, so multiply back
  measured *= 3.3;  // Multiply by 3.3V, our reference voltage
  measured /= 1024.0; // convert to voltage
  return measured;
}

uint8_t voltsToPercent(float volts) {
  // Normalize battery range to 0-100
  const float minv = 3.0;
  const float maxv = 3.7;
  int16_t level = 100 * (volts - minv) / (maxv - minv);
  Log.Debug_(" [Rating 0-100 between %f and %f int: %d]" CR, minv, maxv, (int)level);
  level = MIN(MAX(level, 0), 100); // Peg to 0-100
// debug: Voltage at 9: 4.33 [Rating 0-100 between 2.70 and 5.20 int: 65]
// debug: Voltage at 15: 5.02 [Rating 0-100 between 2.70 and 5.20 int: 92]
// VUSB through divider was 1.7 on battery and 2.5 on USB
  return (uint8_t)level;
}

static void readBatteryVolts() {
  if (gState.getUsbPower()) {
    return; // Don't bother reading meaningless battery level.
  }
  float measured = uiReadSharedVbatPin(VBATPIN);
  float volts = measuredToVoltage(measured);
  // Log.Debug("Voltage at %d (bat): %f\n", (int)VBATPIN, measuredvbat);
  gState.batteryVolts(volts);
}

static void readUSBVolts() {
  float measured = (float)analogRead(VUSBPIN); // 0 to 1023
  float volts = measuredToVoltage(measured);
  // Log.Debug("Voltage at %d (USB): %f\n", (int)VUSBPIN, measured);
  gState.setUsbPower(volts>4.4);
}

bool do_send(const AppState &state, const bool withAck) {
    // Check if there is not a current TX/RX job running
    // if (LMIC.opmode & OP_TXRXPEND) {
    //     Log.Debug(F("OP_TXRXPEND, not sending" CR));
    // }
    // else {

    // Prepare upstream data transmission at the next possible time.
    uint8_t packet[1 + 3 + 3 + 2 + 2 + 1];
    packet[0] = PACKET_FORMAT_ID;
    uint8_t bytes = state.gpsSample().writePacket(packet+1, sizeof(packet)-1);
    assert(bytes+2==sizeof(packet));
    uint8_t bat = 0xFF; // USB powered - battery reading is invalid
    if (!state.getUsbPower()) {
      bat = voltsToPercent(state.batteryVolts());
    }
    packet[sizeof(packet)-1] = bat;

    Log.Debug(F("Writing packet: %*m" CR), sizeof(packet), packet);

    digitalWrite(LED_BUILTIN, HIGH);

    ttn_response_t ret = node.sendBytes(packet, sizeof(packet), '\001' /* port */, withAck);
    if (ret!=TTN_SUCCESSFUL_TRANSMISSION) {
      Log.Error(F("Failed to transmit: %d" CR), ret);
      return false;
    }
    else {
      Log.Debug(F("Packet queued" CR));
      return true;
    }
}

class RespireParameterStore : public RespireStore {
  ParameterStore &_store;
  bool _dirty;

  public:

  RespireParameterStore(ParameterStore &ps)
  : _store(ps), _dirty(false)
  {
  }

  virtual void beginTransaction() {
  }

  virtual void endTransaction() {
    if (_dirty) {
      if (writeParametersToSD(_store)) {
        _dirty = false;
      }
      else {
        Log.Error("Failed to write parameters to store after Respire updates\n");
      }
    }
  }

  virtual bool load(const char *name, uint8_t *bytes, const uint16_t size) {
    return _store.get(name, bytes, size)==size;
  }

  virtual bool load(const char *name, uint32_t *value) {
    return _store.get(name, value);
  }


  virtual bool store(const char *name, const uint8_t *bytes, const uint16_t size) {
    _dirty = true;
    return _store.set(name, bytes, size)==size;
  }

  virtual bool store(const char *name, const uint32_t value) {
    _dirty = true;
    return _store.set(name, value);
  }
};

void setup() {
    pinMode(VBATPIN, INPUT);
    pinMode(VUSBPIN, INPUT);

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);

    Serial.begin(115200);
    Log.Init(LOGLEVEL, Serial);
#if MM_DEBUG_SERIAL
    // Wait for 15 seconds. If no Serial by then, keep going. We are not connected.
    for (int timeout=0; timeout<15 && !Serial; ++timeout) {
      delay(1000);
    }
#endif
    Log.Debug(F("Starting" CR));

    Log.Debug(F("Setup GPS" CR));
    gpsSetup();

    Log.Debug(F("Setup UI" CR));
    uiSetup();

    Log.Debug("Writing default value to NSS: %d\n", LORA_CS);
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH); // Default, unselected

    Log.Debug("Storage setup\n");
    storageSetup();

    Log.Debug(F("Connecting to storage!" CR));
    bool status = byteStore.begin();
    if (!status) {
        Log.Error(F("Could not find a valid bytestore module, check wiring!" CR));
        while (1);
    }
    Log.Debug(F("Initializing parameter store!" CR));
    status = gParameters.begin();
    if (!status) {
        Log.Error(F("Failed to initialize Parameter Store!" CR));
        while (1);
    }
    readParametersFromSD(gParameters);

    Log.Debug(F("Setup RTC" CR));
    gRTC.begin();
    uint32_t realTimeNow = 0; // By default, no, we don't know it.
    if (gRTC.initialized()) {
      DateTime now = gRTC.now();
      // TODO: Check that now is later than compile time
      realTimeNow = now.secondstime();
    }

    Log.Debug(F("Setup Respire" CR));
    RespireParameterStore store(gParameters);

    Log.Debug(F("Init Respire (at %d)" CR), realTimeNow);
    gRespire.init(realTimeNow, &store); // No actions are performed until begin() call below.
    gState.setUsbPower(true);

    Log.Debug(F("Setting lorawan debug mask." CR));
    lorawan.SetDebugMask(Arduino_LoRaWAN::LOG_BASIC | Arduino_LoRaWAN::LOG_ERRORS | Arduino_LoRaWAN::LOG_VERBOSE);
    Log.Debug(F("Registering lorawan event listener." CR));
    status = lorawan.RegisterListener(onEvent, NULL);
    if (!status) {
        Log.Error(F("Failed to register listener!" CR));
        while (1);
    }
    // Log.Debug(F("Personalizing node!" CR));
    // status = node.personalize(devAddr, nwkSKey, appSKey);
    // if (!status) {
    //     Log.Error(F("Failed to personalize device!" CR));
    //     while (1);
    // }
    Log.Debug(F("Provisioning node!" CR));
    status = node.provision(appEui, devEui, appKey);
    if (!status) {
        Log.Error(F("Failed to personalize device!" CR));
        while (1);
    }

    Log.Debug(F("Registering ttn message listener!" CR));
    node.onMessage(onReceive);

    node.begin();

    // Are we already joined? (Do we have session vars APPSKEY, NWKSKEY, and DEVADDR?)
    uint8_t buffer[16];
    bool joined = gParameters.get("APPSKEY", buffer, 16)==PS_SUCCESS;
    joined |= gParameters.get("NWKSKEY", buffer, 16)==PS_SUCCESS;
    uint32_t devaddr;
    joined |= gParameters.get("DEVADDR", &devaddr)==PS_SUCCESS;
    Log.Debug(F("Setting Joined: %T!" CR), joined);
    gState.setJoined(joined);
    if (joined) {
      uint32_t frameUp = 0;
      if (gParameters.get("FCNTUP", &frameUp)==PS_SUCCESS) {
        gState.transmittedFrame(frameUp);
      }
    }

    gTimer.every(10 * 1000, [](){
      // gpsDump(Serial);
      gState.dump();
    });
    gTimer.every(1000, []() {
      readBatteryVolts();
    });
    gTimer.after(500, [](){
      gTimer.every(1000, []() {
        readUSBVolts();
      });
    });

    gRespire.begin();
    gState.dump();

    Log.Debug(F("Setup complete" CR));
}

void loop() {
  // Log.Debug(F("loop" CR)); delay(1000);
  lorawan.loop();
  uiLoop();

  if (!ModeSend.isActive(gState) && !ModeAttemptJoin.isActive(gState)) {
    // Do parsing and timer optional things that could throw off LoRa timing only while NOT sending.
    gpsLoop(Serial);
    gTimer.update();
    gState.setGpsFix(gpsHasFix()); // Quick if value didn't change
  }

  gRespire.loop();
}

void LMIC_DEBUG_PRINTF(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    // char buffer[200];
    // vsnprintf(buffer, sizeof(buffer), fmt, args);
    Log.DebugArgs(fmt, args);
    va_end(args);
}

#ifndef MOCK_ACTIONS

void changeGpsPower(const AppState &state, const AppState &oldState, Mode<AppState> *triggeringMode) {
  // Only called when changed, so just apply value.
  Log.Debug("Setting GPS power: %d\n", state.getGpsPower());
  gpsEnable(state.getGpsPower());
}

void readGpsLocation(const AppState &state, const AppState &oldState, Mode<AppState> *triggeringMode) {
  Log.Debug("Reading GPS location with gps power: %T\n", state.getGpsPower());
  gpsRead([triggeringMode](const GpsSample &gpsSample) {
    Log.Debug("Successfully read GPS\n");
    gRespire.complete(triggeringMode, [&gpsSample](AppState &state){
      state.setGpsLocation(gpsSample);
    });
  }, [triggeringMode]() {
    Log.Error("Failed to read GPS\n");
    gRespire.complete(triggeringMode);
  });
}

void attemptJoin(const AppState &state, const AppState &oldState, Mode<AppState> *triggeringMode) {
  // Enter the AttempJoin state, which is to say, call lorawan.join()
  Log.Debug("Attempting join...\n");
  node.join();
}

void changeSleep(const AppState &state, const AppState &oldState, Mode<AppState> *triggeringMode) {
  // Enter or exit Sleep state
  Log.Debug("Entering sleep mode...\n");
}

void sendLocation(const AppState &state, const AppState &oldState, Mode<AppState> *triggeringMode) {
  // Send location
  Log.Debug("Sending current location...\n");
  if (!do_send(state, false)) {
    Log.Error("Failed do_send\n");
    gRespire.complete(triggeringMode);
  }
  // Action is completed later when TX_COMPLETE event received
}

void sendLocationAck(const AppState &state, const AppState &oldState, Mode<AppState> *triggeringMode) {
  // Send location
  Log.Debug("Sending current location with ACK...\n");
  if (!do_send(state, true)) {
    Log.Error("Failed do_send\n");
    gRespire.complete(triggeringMode);
  }
  // Action is completed later when TX_COMPLETE event received
}

#endif

#endif
