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

#include <SPI.h>
#include <SD.h>

#include <Adafruit_GPS.h>
#include <Arduino_LoRaWAN_ttn.h>
#include <Logging.h>
#include <ParameterStore.h>
#include <RamStore.h>
#include <LoraStack.h>
#include "mm_state.h"

#include <Timer.h>

#include "gps.h"

// #define LOGLEVEL LOG_LEVEL_DEBUG //  _NOOUTPUT, _ERRORS, _WARNINGS, _INFOS, _DEBUG, _VERBOSE

#if !defined(MIN)
#define MIN(a,b) ((a)<(b) ? (a) : (b))
#endif

Clock gClock;
Executor gExecutor;
AppState gState;
RespireContext<AppState> gRespire(gState, ModeMain, &gClock, &gExecutor);

Timer gpsTimer;

// Lorawan Device ID, App ID, and App Key
const char *devEui = "006158A2D06A7A4E";
const char *appEui = "70B3D57EF0001C38";
const char *appKey = "4681950BEFE343C33BD9BB81CA68A89E";

#define PACKET_FORMAT_ID 0x3

// Schedule TX every this many seconds (might become longer due to duty
// cycle limitations).
const unsigned TX_INTERVAL_SEC = 20; // 3600 // Every hour

static constexpr uint8_t LMIC_UNUSED_PIN = 0xff;

// Pin mapping
#define SD_CARD_CS 10
#define LORA_CS 8
const Arduino_LoRaWAN::lmic_pinmap define_lmic_pins = {
// Feather LoRa wiring (with IO1 <--> GPIO#6)
    .nss = LORA_CS,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = 4,
    .dio = {3, 6, LMIC_UNUSED_PIN},
};

const int STORE_SIZE = 2000;
RamStore<STORE_SIZE> byteStore;
ParameterStore pstore(byteStore);
LoraStack_LoRaWAN lorawan(define_lmic_pins, pstore);
LoraStack node(lorawan, pstore, TTN_FP_US915);

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
            gRespire.complete(ModeAttemptJoin, [](AppState &state){
              state.setJoined(true);
            });
            break;
        case EV_RFU1:
            Log.Debug(F("EV_RFU1" CR));
            break;
        case EV_JOIN_FAILED:
            Log.Debug(F("EV_JOIN_FAILED" CR));
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

uint8_t readBatteryLevel() {
    #define VBATPIN A7

    float measuredvbat = analogRead(VBATPIN);
    measuredvbat *= 2;    // we divided by 2, so multiply back
    measuredvbat *= 3.3;  // Multiply by 3.3V, our reference voltage
    measuredvbat /= 1024; // convert to voltage
    Log.Debug("VBat: %f", measuredvbat);

    // Normalize to 0-100
    float minv = 3.2;
    float maxv = 4.2;
    uint8_t level = 100 * (measuredvbat - minv) / (maxv - minv);
    Log.Debug_(" [VBat int: %d]" CR, level);
    if (level<0) {
      level = 0;
    }
    else if (level>100) {
      level = 100;
    }
    return level;
}

uint8_t GpsSample::writePacket(uint8_t *packet, uint8_t packetSize) const {
  int32_t lat = _latitude * 46603;  // Expand +/-180 coordinate to fill 24bits
  uint32_t ulat = lat < 0 ? (UINT32_MAX-(uint32_t)(-lat)+1) : (uint32_t)(lat);
  ulat = htonl(ulat);
  int32_t lon = _longitude * 93206; // Expand +/-90 coordinate to fill 24bits
  uint32_t ulon = lon < 0 ? (UINT32_MAX-(uint32_t)(-lon)+1) : (uint32_t)(lon);
  ulon = htonl(ulon);
  int16_t alt = _altitude;
  alt = htons(alt);
  int16_t hdop = _HDOP * 1000;
  hdop = htons(hdop);

  memcpy(packet + 0, &ulat, 3); // 24 bit
  memcpy(packet + 3, &ulon, 3); // 24 bit
  memcpy(packet + 6,  &alt, 2);
  memcpy(packet + 8, &hdop, 2);

  return 10;
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
    packet[sizeof(packet)-1] = readBatteryLevel();

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

void dumpGps() {
  gpsDump(Serial);
}

void setup() {
    //  attachInterrupt(A0, onA0Change, CHANGE);

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);

    Serial.begin(115200);
    Log.Init(LOGLEVEL, Serial);
    // Wait for 15 seconds. If no Serial by then, keep going. We are not connected.
    for (int timeout=0; timeout<15 && !Serial; ++timeout) {
      delay(1000);
    }
    Log.Debug(F("Starting" CR));

    if (!SD.begin(SD_CARD_CS)) {
      Log.Error("Card failed or not present\n");
      while (1);
    }
    Log.Debug("SD card interface initialized.\n");

    Log.Debug("Writing default value to NSS: %d", LORA_CS);
    // digitalWrite(FRAM_CS, HIGH); // Default, unselected
    // pinMode(FRAM_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH); // Default, unselected
    pinMode(LORA_CS, OUTPUT);

    Log.Debug(F("Connecting to storage!" CR));
    bool status = byteStore.begin();
    if (!status) {
        Log.Error(F("Could not find a valid bytestore module, check wiring!" CR));
        while (1);
    }
    Log.Debug(F("Initializing parameter store!" CR));
    status = pstore.begin();
    if (!status) {
        Log.Error(F("Failed to initialize Parameter Store!" CR));
        while (1);
    }
    // pstore.set("FCNTUP", SEQ_NO_20180213);

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

    // LMIC init
    // Log.Debug(F("Begin lorawan interaction!" CR));
    // lorawan.begin();

    // // Disable link check validation
    // Log.Debug(F("LMIC_setLinkCheckMode" CR));
    // LMIC_setLinkCheckMode(0);

    // // TTN uses SF9 for its RX2 window.
    // LMIC.dn2Dr = DR_SF9;

    // // Set data rate and transmit power for uplink (note: txpow seems to be ignored by the library)
    // Log.Debug(F("LMIC_setDrTxpow" CR));
    // LMIC_setDrTxpow(DR_SF7, 14);

    gpsTimer.every(60 * 1000, dumpGps);

    Log.Debug(F("Setup GPS" CR));
    gpsSetup();

    Log.Debug(F("Setup Respire" CR));
    gRespire.init();
    gState.setUsbPower(true);
    gState.setGpsFix(true);
    gRespire.begin();
    gState.dump();

    Log.Debug(F("Setup complete" CR));
}

void loop() {
  // Log.Debug(F("os_runloop_once" CR));
  lorawan.loop();
  gpsLoop(Serial);

  gpsTimer.update();

  gState.setGpsFix(gpsHasFix()); // Quick if value didn't change

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

void changeGpsPower(const AppState &state, const AppState &oldState, Mode *triggeringMode) {
  // Only called when changed, so just apply value.
  Log.Debug("Setting GPS power: %d", state.getGpsPower());
  gpsEnable(state.getGpsPower());
}

void readGpsLocation(const AppState &state, const AppState &oldState, Mode *triggeringMode) {
  Log.Debug("Reading GPS location: %d", state.getGpsPower());
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

void attemptJoin(const AppState &state, const AppState &oldState, Mode *triggeringMode) {
  // Enter the AttempJoin state, which is to say, call lorawan.join()
  Log.Debug("Attempting join...");
  node.join();
}

void changeSleep(const AppState &state, const AppState &oldState, Mode *triggeringMode) {
  // Enter or exit Sleep state
  Log.Debug("Entering sleep mode...\n");
}

void writeLocation(const AppState &state, const AppState &oldState, Mode *triggeringMode) {
  char filename[300];
  const GpsSample &gps = state.gpsSample();
  sprintf(filename, "/gps/%04d%02d%02d/%02d%02d.csv", (int)gps._year, (int)gps._month, (int)gps._day, (int)gps._hour, (int)gps._minute);

  char dataString[300];
  sprintf(dataString, "%04d%02d%02d:%02d%02d%02d.%03d,%f,%f,%f,%f,battery,frame", // TODO frame & battery
        (int)gps._year, (int)gps._month, (int)gps._day, (int)gps._hour, (int)gps._minute, (int)gps._seconds, (int)gps._millis,
        gps._latitude, gps._longitude, gps._altitude, gps._HDOP /*, state.ttnFrameUp(), state.batteryLevel() */);

  Log.Debug("Writing \"%s\" to file \"%s\"\n", dataString, filename);

  File dataFile = SD.open(filename, FILE_WRITE);
  if (dataFile) {
    dataFile.println(dataString);
    dataFile.close();
  }
  else {
    Log.Error("error opening %s\n", filename);
  }
  gRespire.complete(triggeringMode);
}

void sendLocation(const AppState &state, const AppState &oldState, Mode *triggeringMode) {
  // Send location
  Log.Debug("Sending current location...\n");
  if (!do_send(state, false)) {

  }
  else {

  }
  gRespire.complete(triggeringMode);
}

void sendLocationAck(const AppState &state, const AppState &oldState, Mode *triggeringMode) {
  // Send location
  Log.Debug("Sending current location with ACK...\n");
  if (!do_send(state, true)) {

  }
  else {

  }
  gRespire.complete(triggeringMode);
}

#endif