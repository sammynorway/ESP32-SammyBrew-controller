#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoWebsockets.h>
#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ArduinoOTA.h>
#include "secrets.h"
using namespace websockets;

WiFiServer telnetServer(23);
WiFiClient telnetClient;


// ============================================================================
// DS18B20 temperature sensor
// ============================================================================

const uint8_t ONE_WIRE_PIN = 4;
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature sensors(&oneWire);

//28:56:27:AD:09:00:00:91
DeviceAddress kettleSensorAddress = {
    0x28, 0x56, 0x27, 0xAD,
    0x09, 0x00, 0x00, 0x91
};

//28:94:92:AC:09:00:00:72
DeviceAddress hltSensorAddress = {
    0x28, 0x94, 0x92, 0xAC,
    0x09, 0x00, 0x00, 0x72
};

// ============================================================================
// Heater control
// ============================================================================

const uint8_t KETTLE_SSR_PIN = 5;

bool requestedHeaterState = false;
bool kettleSsrOn = false;

float requestedPower = 0.0;
float effectivePower = 0.0;

const unsigned long SSR_WINDOW_MS = 100;
unsigned long kettleSsrWindowStart = 0;

// ============================================================================
// Heater HLT control
// ============================================================================

const uint8_t HLT_SSR_PIN = 6;

bool hltSsrOn = false;
bool requestedHltState = false;

float requestedHltPower = 0.0;
float effectiveHltPower = 0.0;

unsigned long lastHltHeaterCommand = 0;
unsigned long hltSsrWindowStart = 0;

// ============================================================================
// Circulation pump control
// ============================================================================

const uint8_t CIRCULATION_PUMP_PIN = 7;
const uint8_t CIRCULATION_PUMP_ACTIVE_LEVEL = HIGH;

bool requestedPumpState = false;
bool circulationPumpOn = false;

// ============================================================================
// WiFi
// ============================================================================

// ============================================================================
// CraftBeerPi
// ============================================================================

const char* WS_HOST = "192.168.150.83";
const uint16_t WS_PORT = 8000;
const char* WS_PATH = "/esp32/ws";

// ============================================================================
// Timing
// ============================================================================

const unsigned long RECONNECT_INTERVAL_MS = 5000;
const unsigned long APPLICATION_PING_INTERVAL_MS = 5000;
const unsigned long TEMPERATURE_SEND_INTERVAL_MS = 1000;

const unsigned long PONG_TIMEOUT_MS = 15000;

// Hvis CBPi har slått heater ON, må vi få en ny heater-kommando
// innen denne tiden.
// PIDBoil sample time er nå 2 sek, så 8 sek gir god margin.
// const unsigned long HEATER_COMMAND_TIMEOUT_MS = 30000000000000;
// Maks alder på temperaturmåling

const unsigned long TEMPERATURE_TIMEOUT_MS = 5000;

unsigned long lastTemperatureRead = 0;

const unsigned long TEMPERATURE_READ_INTERVAL_MS = 1000;

// ============================================================================
// Temperature safety
// ============================================================================

// DS18B20.

float kettleTemperature = NAN;
float hltTemperature = NAN;

unsigned long lastKettleTemperatureUpdate = 0;
unsigned long lastHltTemperatureUpdate = 0;

// Absolutt software-grense.
// Kan senere gjøres konfigurerbar.

const float KETTLE_MAX_TEMP_C = 105.0;
const float HLT_MAX_TEMP_C = 105.0;

// Minimum/maximum plausible sensor values

const float SENSOR_MIN_TEMP_C = -20.0;
const float SENSOR_MAX_TEMP_C = 125.0;

// ============================================================================
// Safety / heater state
// ============================================================================

enum class FaultCode {
    NONE,
    WIFI_LOST,
    WEBSOCKET_LOST,
    HANDSHAKE_MISSING,
    HEATER_COMMAND_TIMEOUT,
    TEMPERATURE_STALE,
    TEMPERATURE_INVALID,
    OVERTEMPERATURE
};

FaultCode currentFault = FaultCode::NONE;
FaultCode currentHltFault = FaultCode::NONE;
FaultCode currentPumpFault = FaultCode::NONE;

bool websocketConnected = false;
bool handshakeComplete = false;

unsigned long lastReconnectAttempt = 0;
unsigned long lastApplicationPing = 0;
unsigned long lastTemperatureSend = 0;
unsigned long lastHeaterCommand = 0;
unsigned long lastPongReceived = 0;
unsigned long lastServerMessage = 0;

// ============================================================================
// WebSocket
// ============================================================================

WebsocketsClient wsClient;

void setKettleSSR(bool on);
void setHltSSR(bool on);
void setCirculationPump(bool on);

// ============================================================================
// Helpers
// ============================================================================

const char* faultToString(FaultCode fault) {
    switch (fault) {
        case FaultCode::NONE:
            return "NONE";
        case FaultCode::WIFI_LOST:
            return "WIFI_LOST";
        case FaultCode::WEBSOCKET_LOST:
            return "WEBSOCKET_LOST";
        case FaultCode::HANDSHAKE_MISSING:
            return "HANDSHAKE_MISSING";
        case FaultCode::HEATER_COMMAND_TIMEOUT:
            return "HEATER_COMMAND_TIMEOUT";
        case FaultCode::TEMPERATURE_STALE:
            return "TEMPERATURE_STALE";
        case FaultCode::TEMPERATURE_INVALID:
            return "TEMPERATURE_INVALID";
        case FaultCode::OVERTEMPERATURE:
            return "OVERTEMPERATURE";
        default:
            return "UNKNOWN";
    }

}
// ============================================================================
// Logging to both Serial and Telnet
// ============================================================================

void logPrint(const String& msg) {
    Serial.print(msg);

    if (telnetClient && telnetClient.connected()) {
        telnetClient.print(msg);
    }
}

void logLine(const String& msg) {
    Serial.println(msg);

    if (telnetClient && telnetClient.connected()) {
        telnetClient.println(msg);
    }
}

// Blank line
void logLine() {
    Serial.println();

    if (telnetClient && telnetClient.connected()) {
        telnetClient.println();
    }
}

void logPrint(float value, int decimals = 2) {
    Serial.print(value, decimals);

    if (telnetClient && telnetClient.connected()) {
        telnetClient.print(value, decimals);
    }
}

void logLine(float value, int decimals = 2) {
    Serial.println(value, decimals);

    if (telnetClient && telnetClient.connected()) {
        telnetClient.println(value, decimals);
    }
}

void logPrint(int value) {
    Serial.print(value);

    if (telnetClient && telnetClient.connected()) {
        telnetClient.print(value);
    }
}

void logLine(int value) {
    Serial.println(value);

    if (telnetClient && telnetClient.connected()) {
        telnetClient.println(value);
    }
}

void logPrint(unsigned long value) {
    Serial.print(value);

    if (telnetClient && telnetClient.connected()) {
        telnetClient.print(value);
    }
}

void logLine(unsigned long value) {
    Serial.println(value);

    if (telnetClient && telnetClient.connected()) {
        telnetClient.println(value);
    }
}

// Used for DS18B20 address printing with HEX
void logPrint(uint8_t value, int format) {
    Serial.print(value, format);

    if (telnetClient && telnetClient.connected()) {
        telnetClient.print(value, format);
    }
}

// IP address
void logPrint(const IPAddress& ip) {
    Serial.print(ip);

    if (telnetClient && telnetClient.connected()) {
        telnetClient.print(ip);
    }
}

void logLine(const IPAddress& ip) {
    Serial.println(ip);

    if (telnetClient && telnetClient.connected()) {
        telnetClient.println(ip);
    }
}

// ============================================================================
// Heater output
// ============================================================================

void applyHeaterOutput(float power) {
    if (power < 0.0) {
        power = 0.0;
    }
    if (power > 100.0) {
        power = 100.0;
    }
    // Foreløpig bare lagre/logge.
    // INGEN GPIO skrus på her.
    effectivePower = power;
}

void forceHeaterOff(FaultCode fault) {
    if (
        effectivePower != 0.0 ||
        currentFault != fault
    ) {
        logLine();
        logLine("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
        logLine("HEATER SAFETY SHUTDOWN");
        logPrint("Fault: ");
        logLine(faultToString(fault));
        logLine("Effective heater power -> 0 %");
        logLine("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
        logLine();

    }

    currentFault = fault;
    applyHeaterOutput(0.0);
    setKettleSSR(false);

}

void applyHltHeaterOutput(float power) {
    if (power < 0.0) {
        power = 0.0;
    }
    if (power > 100.0) {
        power = 100.0;
    }
    effectiveHltPower = power;
}

void forceHltHeaterOff(FaultCode fault) {
    if (
        effectiveHltPower != 0.0 ||
        currentHltFault != fault
    ) {
        logLine();
        logLine("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
        logLine("HLT HEATER SAFETY SHUTDOWN");
        logPrint("Fault: ");
        logLine(faultToString(fault));
        logLine("Effective HLT heater power -> 0 %");
        logLine("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
        logLine();
    }

    currentHltFault = fault;
    applyHltHeaterOutput(0.0);
    setHltSSR(false);
}

void forceCirculationPumpOff(FaultCode fault) {
    if (
        circulationPumpOn ||
        currentPumpFault != fault
    ) {
        logLine();
        logLine("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
        logLine("CIRCULATION PUMP SAFETY SHUTDOWN");
        logPrint("Fault: ");
        logLine(faultToString(fault));
        logLine("Circulation pump -> OFF");
        logLine("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
        logLine();
    }

    currentPumpFault = fault;
    setCirculationPump(false);
}

// ============================================================================
// Temperature validation
// ============================================================================

bool temperatureValid() {
    if (isnan(kettleTemperature)) {
        return false;
    }
    if (kettleTemperature < SENSOR_MIN_TEMP_C) {
        return false;
    }
    if (kettleTemperature > SENSOR_MAX_TEMP_C) {
        return false;
    }
    return true;
}

bool hltTemperatureValid() {
    if (isnan(hltTemperature)) {
        return false;
    }
    if (hltTemperature < SENSOR_MIN_TEMP_C) {
        return false;
    }
    if (hltTemperature > SENSOR_MAX_TEMP_C) {
        return false;
    }
    return true;
}

// ============================================================================
// Global safety evaluation
// ============================================================================

bool heaterSafetyOK() {
    // ------------------------------------------------------------
    // WiFi
    // ------------------------------------------------------------
    if (WiFi.status() != WL_CONNECTED) {
        forceHeaterOff(
            FaultCode::WIFI_LOST
        );
        return false;
    }

    // ------------------------------------------------------------
    // WebSocket
    // ------------------------------------------------------------

    if (!websocketConnected) {
        forceHeaterOff(
            FaultCode::WEBSOCKET_LOST
        );
        return false;
    }

    // ------------------------------------------------------------
    // Handshake
    // ------------------------------------------------------------

    if (!handshakeComplete) {
        forceHeaterOff(
            FaultCode::HANDSHAKE_MISSING
        );
        return false;
    }

    // ------------------------------------------------------------
    // Temperature validity
    // ------------------------------------------------------------

    if (!temperatureValid()) {
        forceHeaterOff(
            FaultCode::TEMPERATURE_INVALID
        );
        return false;
    }

    // ------------------------------------------------------------
    // Temperature freshness
    // ------------------------------------------------------------

    if (
        millis() - lastKettleTemperatureUpdate
        > TEMPERATURE_TIMEOUT_MS
    ) {
        forceHeaterOff(
            FaultCode::TEMPERATURE_STALE
        );
        return false;
    }

    // ------------------------------------------------------------
    // Absolute temperature limit
    // ------------------------------------------------------------

    if (
        kettleTemperature
        >= KETTLE_MAX_TEMP_C
    ) {
        forceHeaterOff(
            FaultCode::OVERTEMPERATURE
        );
        return false;
    }

    // ------------------------------------------------------------
    // Heater command freshness
    //
    // Bare relevant dersom CBPi faktisk ber om varme.
    // ------------------------------------------------------------

    if (requestedHeaterState) {
        if (
            millis() - lastPongReceived
            > PONG_TIMEOUT_MS
        ) {
            forceHeaterOff(
                FaultCode::WEBSOCKET_LOST
            );
            return false;
        }
    }

    // ------------------------------------------------------------
    // All checks passed
    // ------------------------------------------------------------

    currentFault = FaultCode::NONE;
    return true;

}

// ============================================================================
// Recalculate actual heater output
// ============================================================================

void updateHeaterSafety() {
    if (!requestedHeaterState) {
        currentFault = FaultCode::NONE;
        applyHeaterOutput(0.0);
        return;
    }

    if (!heaterSafetyOK()) {
        return;
    }

    // CraftBeerPi asks for heating

    // AND every safety check is OK.

    applyHeaterOutput(
        requestedPower
    );

}

bool hltHeaterSafetyOK() {
    if (WiFi.status() != WL_CONNECTED) {
        forceHltHeaterOff(FaultCode::WIFI_LOST);
        return false;
    }

    if (!websocketConnected) {
        forceHltHeaterOff(FaultCode::WEBSOCKET_LOST);
        return false;
    }

    if (!handshakeComplete) {
        forceHltHeaterOff(FaultCode::HANDSHAKE_MISSING);
        return false;
    }

    if (!hltTemperatureValid()) {
        forceHltHeaterOff(FaultCode::TEMPERATURE_INVALID);
        return false;
    }

    if (
        millis() - lastHltTemperatureUpdate
        > TEMPERATURE_TIMEOUT_MS
    ) {
        forceHltHeaterOff(FaultCode::TEMPERATURE_STALE);
        return false;
    }

    if (hltTemperature >= HLT_MAX_TEMP_C) {
        forceHltHeaterOff(FaultCode::OVERTEMPERATURE);
        return false;
    }

    if (
        requestedHltState &&
        millis() - lastPongReceived > PONG_TIMEOUT_MS
    ) {
        forceHltHeaterOff(FaultCode::WEBSOCKET_LOST);
        return false;
    }

    currentHltFault = FaultCode::NONE;
    return true;
}

void updateHltHeaterSafety() {
    if (!requestedHltState) {
        currentHltFault = FaultCode::NONE;
        applyHltHeaterOutput(0.0);
        return;
    }

    if (!hltHeaterSafetyOK()) {
        return;
    }

    applyHltHeaterOutput(requestedHltPower);
}

bool circulationPumpSafetyOK() {
    if (WiFi.status() != WL_CONNECTED) {
        forceCirculationPumpOff(FaultCode::WIFI_LOST);
        return false;
    }

    if (!websocketConnected) {
        forceCirculationPumpOff(FaultCode::WEBSOCKET_LOST);
        return false;
    }

    if (!handshakeComplete) {
        forceCirculationPumpOff(FaultCode::HANDSHAKE_MISSING);
        return false;
    }

    if (
        requestedPumpState &&
        millis() - lastPongReceived > PONG_TIMEOUT_MS
    ) {
        forceCirculationPumpOff(FaultCode::WEBSOCKET_LOST);
        return false;
    }

    currentPumpFault = FaultCode::NONE;
    return true;
}

void updateCirculationPumpSafety() {
    if (!requestedPumpState) {
        currentPumpFault = FaultCode::NONE;
        setCirculationPump(false);
        return;
    }

    if (!circulationPumpSafetyOK()) {
        return;
    }

    setCirculationPump(true);
}

// ============================================================================
// Print sensor address
// ============================================================================

void printSensorAddress(DeviceAddress address) {
    for (uint8_t i = 0; i < 8; i++) {

        if (address[i] < 16) {
            logPrint("0");
        }

        logPrint(address[i], HEX);

        if (i < 7) {
            logPrint(":");
        }
    }
}

void scanTemperatureSensors() {
    logLine();
    logLine("=== DS18B20 SENSOR SCAN ===");
    int count = sensors.getDeviceCount();
    logPrint("Sensors found: ");
    logLine(count);
    DeviceAddress address;
    for (int i = 0; i < count; i++) {
        logPrint("Sensor ");
        logPrint(i);
        logPrint(": ");
        if (
            sensors.getAddress(
                address,
                i
            )
        ) {
            printSensorAddress(
                address
            );
        } else {
            logPrint(
                "Unable to read address"
            );
        }
        logLine();
    }
    logLine(
        "============================"
    );
    logLine();
}


bool updateTemperatureSensor() {
    sensors.requestTemperatures();
    // ============================================================
    // Bryggekjele
    // ============================================================
    float kettleTemp =
        sensors.getTempC(kettleSensorAddress);
    bool kettleValid = true;
    if (kettleTemp == DEVICE_DISCONNECTED_C) {
        logLine(
            "KETTLE DS18B20 ERROR: sensor disconnected"
        );
        kettleValid = false;
    }
    if (kettleTemp == 85.0) {
        logLine(
            "KETTLE DS18B20 ERROR: invalid 85 C reading"
        );
        kettleValid = false;
    }

    if (isnan(kettleTemp)) {
        logLine(
            "KETTLE DS18B20 ERROR: NaN"
        );
        kettleValid = false;
    }

    if (
        kettleTemp < SENSOR_MIN_TEMP_C ||
        kettleTemp > SENSOR_MAX_TEMP_C
    ) {

        logPrint(
            "KETTLE DS18B20 ERROR: out of range: "
        );
        logLine(
            kettleTemp
        );
        kettleValid = false;
    }

    if (kettleValid) {
        kettleTemperature =
            kettleTemp;
        lastKettleTemperatureUpdate =
            millis();
        logPrint(
            "Kettle: "
        );

        logPrint(
            kettleTemperature,
            2
        );
        logLine(" C");
    }

    // ============================================================
    // Skyllevann / HLT
    // ============================================================

    float hltTemp =
        sensors.getTempC(hltSensorAddress);
    bool hltValid = true;
    if (hltTemp == DEVICE_DISCONNECTED_C) {
        logLine(
            "HLT DS18B20 ERROR: sensor disconnected"
        );
        hltValid = false;
    }

    if (hltTemp == 85.0) {
        logLine(
            "HLT DS18B20 ERROR: invalid 85 C reading"
        );
        hltValid = false;

    }

    if (isnan(hltTemp)) {
        logLine(
            "HLT DS18B20 ERROR: NaN"
        );
        hltValid = false;
    }

    if (
        hltTemp < SENSOR_MIN_TEMP_C ||
        hltTemp > SENSOR_MAX_TEMP_C
    ) {

        logPrint(
            "HLT DS18B20 ERROR: out of range: "
        );

        logLine(
            hltTemp
        );

        hltValid = false;
    }

    if (hltValid) {
        hltTemperature =
            hltTemp;
        lastHltTemperatureUpdate =
            millis();
        logPrint(
            "HLT: "
        );

        logPrint(
            hltTemperature,
            2
        );

        logLine(" C");

    }

    // true hvis minst én sensor ga gyldig verdi

    return kettleValid || hltValid;

}

// ============================================================================
// WebSocket messages
// ============================================================================

void sendHello() {
    JsonDocument doc;
    doc["type"] = "hello";
    doc["device"] = "brew-controller";
    doc["firmware"] = "0.4.0";
    String msg;
    serializeJson(
        doc,
        msg
    );

    logPrint("TX: ");
    logLine(msg);
    wsClient.send(
        msg
    );

}

void sendApplicationPing() {
    JsonDocument doc;
    doc["type"] = "ping";
    String msg;
    serializeJson(
        doc,
        msg
    );

    wsClient.send(
        msg
    );

}

void sendTemperature() {

    // ------------------------------------------------------------
    // Kettle
    // ------------------------------------------------------------

    if (!isnan(kettleTemperature)) {
        JsonDocument kettleDoc;
        kettleDoc["type"] = "temperature";
        kettleDoc["sensor"] = "kettle";
        kettleDoc["value"] = kettleTemperature;
        String kettleMsg;
        serializeJson(
            kettleDoc,
            kettleMsg
        );

        logPrint("TX KETTLE: ");
        logLine(kettleMsg);
        wsClient.send(
            kettleMsg
        );

    }

    // ------------------------------------------------------------
    // HLT / skyllevann
    // ------------------------------------------------------------

    if (!isnan(hltTemperature)) {
        JsonDocument hltDoc;
        hltDoc["type"] = "temperature";
        hltDoc["sensor"] = "hlt";
        hltDoc["value"] = hltTemperature;
        String hltMsg;
        serializeJson(
            hltDoc,
            hltMsg
        );

        logPrint("TX HLT: ");
        logLine(hltMsg);
        wsClient.send(
            hltMsg
        );

    }

}

// ============================================================================
// Heater ACK
//
// Legg merke til:
// Vi returnerer både requested og effective power.
// Det blir veldig nyttig senere.

// ============================================================================

void sendHeaterAck() {
    if (!websocketConnected) {
        return;
    }

    JsonDocument ack;
    ack["type"] = "heater_ack";
    ack["heater"] = "kettle";
    ack["ssr_on"] = kettleSsrOn;
    ack["state"] = requestedHeaterState;
    ack["requested_power"] = requestedPower;
    ack["effective_power"] = effectivePower;
    ack["fault"] = faultToString(
        currentFault
    );

    String msg;
    serializeJson(
        ack,
        msg
    );

    wsClient.send(
        msg
    );

}

void sendHltHeaterAck() {
    if (!websocketConnected) {
        return;
    }

    JsonDocument ack;
    ack["type"] = "heater_ack";
    ack["heater"] = "hlt";
    ack["ssr_on"] = hltSsrOn;
    ack["state"] = requestedHltState;
    ack["requested_power"] = requestedHltPower;
    ack["effective_power"] = effectiveHltPower;
    ack["fault"] = faultToString(currentHltFault);

    String msg;
    serializeJson(ack, msg);
    wsClient.send(msg);
}

void sendCirculationPumpAck() {
    if (!websocketConnected) {
        return;
    }

    JsonDocument ack;
    ack["type"] = "pump_ack";
    ack["pump"] = "circulation";
    ack["state"] = circulationPumpOn;
    ack["fault"] = faultToString(currentPumpFault);

    String msg;
    serializeJson(ack, msg);
    wsClient.send(msg);
}

// ============================================================================
// Incoming WebSocket message
// ============================================================================

void onWebSocketMessage(
    WebsocketsMessage message
) {
    lastServerMessage = millis();
    logPrint("RX: ");
    logLine(
        message.data()
    );
    JsonDocument doc;
    DeserializationError err =
        deserializeJson(
            doc,
            message.data()
        );

    if (err) {
        logPrint(
            "JSON error: "
        );

        logLine(
            err.c_str()
        );
        return;
    }

    const char* type =
        doc["type"];
    if (!type) {
        return;
    }

    // ------------------------------------------------------------
    // Handshake ACK
    // ------------------------------------------------------------

    if (
        strcmp(
            type,
            "hello_ack"
        ) == 0
    ) {
        handshakeComplete = true;
        lastPongReceived = millis();
        logLine(
            "CraftBeerPi handshake OK"
        );
        updateHeaterSafety();
        updateHltHeaterSafety();
        updateCirculationPumpSafety();
        return;
    }

    // ------------------------------------------------------------
    // Pong
    // ------------------------------------------------------------

    if (
        strcmp(
            type,
            "pong"
        ) == 0
    ) {
        lastPongReceived = millis();

        logPrint("PONG received. Age reset to ");
        logPrint(lastPongReceived);
        logLine(" ms");

        return;
    }

    // ------------------------------------------------------------
    // Circulation pump command from CBPi
    // ------------------------------------------------------------

    if (
        strcmp(
            type,
            "pump_state"
        ) == 0
    ) {
        const char* pump =
            doc["pump"] | "circulation";

        if (strcmp(pump, "circulation") != 0) {
            logPrint("Unknown pump channel: ");
            logLine(pump);
            return;
        }

        requestedPumpState =
            doc["state"] | false;

        updateCirculationPumpSafety();

        logLine();
        logLine("==============================");
        logLine("CIRCULATION PUMP COMMAND");
        logPrint("Requested state: ");
        logLine(requestedPumpState ? "ON" : "OFF");
        logPrint("Effective state: ");
        logLine(circulationPumpOn ? "ON" : "OFF");
        logPrint("Fault: ");
        logLine(faultToString(currentPumpFault));
        logLine("==============================");

        sendCirculationPumpAck();
        return;
    }

    // ------------------------------------------------------------
    // Temperature ACK
    // ------------------------------------------------------------

    if (
        strcmp(
            type,
            "temperature_ack"
        ) == 0

    ) {
        return;
    }

    // ------------------------------------------------------------
    // Heater command from CBPi
    // ------------------------------------------------------------

    if (
        strcmp(
            type,
            "heater_power"
        ) == 0
    ) {
        const char* heater =
            doc["heater"] | "kettle";
        bool state =
            doc["state"] | false;
        float power =
            doc["power"] | 0.0;
        // Clamp CBPi request
        if (power < 0.0) {
            power = 0.0;
        }

        if (power > 100.0) {
            power = 100.0;
        }

        // OFF always means 0 %

        if (!state) {
            power = 0.0;
        }

        if (strcmp(heater, "hlt") == 0) {
            requestedHltState = state;
            requestedHltPower = power;
            lastHltHeaterCommand = millis();

            updateHltHeaterSafety();

            logLine();
            logLine("==============================");
            logLine("HLT HEATER COMMAND");
            logPrint("Requested state: ");
            logLine(requestedHltState ? "ON" : "OFF");
            logPrint("Requested power: ");
            logPrint(requestedHltPower, 1);
            logLine(" %");
            logPrint("Effective power: ");
            logPrint(effectiveHltPower, 1);
            logLine(" %");
            logPrint("Fault: ");
            logLine(faultToString(currentHltFault));
            logLine("==============================");

            sendHltHeaterAck();
            return;
        }

        if (strcmp(heater, "kettle") != 0) {
            logPrint("Unknown heater channel: ");
            logLine(heater);
            return;
        }

        requestedHeaterState = state;
        requestedPower = power;
        lastHeaterCommand =
            millis();
        // Run ALL safety checks before accepting output.
        updateHeaterSafety();
        logLine();
        logLine(
            "=============================="
        );

        logLine(
            "HEATER COMMAND"
        );

        logPrint(
            "Requested state: "
        );

        logLine(
            requestedHeaterState
                ? "ON"
                : "OFF"
        );

        logPrint(
            "Requested power: "
        );

        logPrint(
            requestedPower,
            1
        );

        logLine(" %");
        logPrint(
            "Effective power: "
        );

        logPrint(
            effectivePower,
            1
        );

        logLine(" %");
        logPrint(
            "Fault: "
        );

        logLine(
            faultToString(
                currentFault
            )

        );
        logLine(
            "=============================="
        );

        sendHeaterAck();
        return;
    }

}

// ============================================================================
// WebSocket events
// ============================================================================

void onWebSocketEvent(
    WebsocketsEvent event,
    String data
) {
    switch (event) {
        case WebsocketsEvent::ConnectionOpened:
            logLine("WebSocket connected");

            websocketConnected = true;
            handshakeComplete = false;

            // Connecting must NEVER enable heat.

            forceHeaterOff(
                FaultCode::HANDSHAKE_MISSING
            );
            forceHltHeaterOff(
                FaultCode::HANDSHAKE_MISSING
            );
            forceCirculationPumpOff(
                FaultCode::HANDSHAKE_MISSING
            );

            sendHello();

            break;

        case WebsocketsEvent::ConnectionClosed:
            logLine(
                "WebSocket disconnected"
            );

            websocketConnected = false;
            handshakeComplete = false;

            // Immediate fail-safe.

            forceHeaterOff(
                FaultCode::WEBSOCKET_LOST
            );
            forceHltHeaterOff(
                FaultCode::WEBSOCKET_LOST
            );
            forceCirculationPumpOff(
                FaultCode::WEBSOCKET_LOST
            );

            break;
        case WebsocketsEvent::GotPing:

            logLine(
                "WS protocol ping received"
            );

            // Reply to aiohttp heartbeat.

            wsClient.pong(
                data
            );

            break;
        case WebsocketsEvent::GotPong:
            break;
    }
}

// ============================================================================
// WiFi
// ============================================================================

void connectWiFi() {
    logPrint(
        "Connecting WiFi"
    );
    WiFi.mode(
        WIFI_STA
    );
    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );

    while (
        WiFi.status()
        != WL_CONNECTED
    ) {

        delay(500);
        logPrint(".");
    }

    logLine();
    logLine(
        "WiFi connected"
    );

    logPrint(
        "IP: "
    );

    logLine(
        WiFi.localIP()
    );

}

// ============================================================================
// WebSocket connect
// ============================================================================

bool connectWebSocket() {
    logPrint(
        "Connecting WebSocket to ws://"
    );

    logPrint(
        WS_HOST
    );

    logPrint(":");
    logPrint(
        WS_PORT
    );

    logLine(
        WS_PATH
    );

    bool ok =
        wsClient.connect(
            WS_HOST,
            WS_PORT,
            WS_PATH
        );

    if (!ok) {
        logLine(
            "WebSocket connection failed"
        );

        websocketConnected = false;
        handshakeComplete = false;

        forceHeaterOff(
            FaultCode::WEBSOCKET_LOST
        );
        forceHltHeaterOff(
            FaultCode::WEBSOCKET_LOST
        );
        forceCirculationPumpOff(
            FaultCode::WEBSOCKET_LOST
        );
    }
    return ok;
}
// ============================================================================
// kettle SSR
// ============================================================================

void setKettleSSR(bool on) {
    kettleSsrOn = on;
    digitalWrite(
        KETTLE_SSR_PIN,
        on ? HIGH : LOW
    );
}

void updateKettleSSR() {

    unsigned long now = millis();

    if (
        now - kettleSsrWindowStart
        >= SSR_WINDOW_MS
    ) {
        kettleSsrWindowStart = now;
    }

    // Fail-safe OFF
    if (
        effectivePower <= 0.0 ||
        currentFault != FaultCode::NONE ||
        !requestedHeaterState
    ) {

        setKettleSSR(false);
        return;
    }

    unsigned long onTime =
        (unsigned long)(
            SSR_WINDOW_MS *
            (effectivePower / 100.0)
        );

    unsigned long positionInWindow =
        now - kettleSsrWindowStart;

    if (positionInWindow < onTime) {

        setKettleSSR(true);

    } else {

        setKettleSSR(false);
    }

}

// ============================================================================
// HLT SSR
// ============================================================================

void setHltSSR(bool on) {
    hltSsrOn = on;
    digitalWrite(
        HLT_SSR_PIN,
        on ? HIGH : LOW
    );
}

void updateHltSSR() {
    unsigned long now = millis();

    if (
        now - hltSsrWindowStart
        >= SSR_WINDOW_MS
    ) {
        hltSsrWindowStart = now;
    }

    if (
        effectiveHltPower <= 0.0 ||
        currentHltFault != FaultCode::NONE ||
        !requestedHltState
    ) {
        setHltSSR(false);
        return;
    }

    unsigned long onTime =
        (unsigned long)(
            SSR_WINDOW_MS *
            (effectiveHltPower / 100.0)
        );

    unsigned long positionInWindow =
        now - hltSsrWindowStart;

    setHltSSR(positionInWindow < onTime);
}

// ============================================================================
// Circulation pump output
// ============================================================================

void setCirculationPump(bool on) {
    circulationPumpOn = on;
    digitalWrite(
        CIRCULATION_PUMP_PIN,
        on
            ? CIRCULATION_PUMP_ACTIVE_LEVEL
            : (CIRCULATION_PUMP_ACTIVE_LEVEL == HIGH ? LOW : HIGH)
    );
}


// ============================================================================
// Setup
// ============================================================================

void setup() {
    Serial.begin(
        115200
    );


    // Kettle SSR OFF by default
    pinMode(
        KETTLE_SSR_PIN,
        OUTPUT
    );
    digitalWrite(
        KETTLE_SSR_PIN,
        LOW
    );

    // HLT SSR OFF by default
    pinMode(
        HLT_SSR_PIN,
        OUTPUT
    );
    digitalWrite(
        HLT_SSR_PIN,
        LOW
    );

    pinMode(
        CIRCULATION_PUMP_PIN,
        OUTPUT
    );
    setCirculationPump(false);

    // Give USB CDC serial monitor time to reconnect.

    unsigned long serialStart =
        millis();

    while (
        !Serial &&
        millis() - serialStart < 5000
    ) {
        delay(10);
    }

    delay(1500);
    logLine();
    logLine(
        "=== SammyBrew Controller ==="
    );

    // ------------------------------------------------------------
    // ABSOLUTE SAFE BOOT STATE
    // ------------------------------------------------------------

    requestedHeaterState = false;
    requestedPower = 0.0;
    effectivePower = 0.0;

    requestedHltState = false;
    requestedHltPower = 0.0;
    effectiveHltPower = 0.0;

    requestedPumpState = false;
    circulationPumpOn = false;

    currentFault =
        FaultCode::WEBSOCKET_LOST;
    currentHltFault =
        FaultCode::WEBSOCKET_LOST;
    currentPumpFault =
        FaultCode::WEBSOCKET_LOST;
    // Initial fake temperature read


    connectWiFi();

    telnetServer.begin();
    telnetServer.setNoDelay(true);

    logLine("Telnet server started on port 23");

    wsClient.onMessage(
        onWebSocketMessage
    );

    wsClient.onEvent(
        onWebSocketEvent
    );

    connectWebSocket();

    sensors.begin();
    scanTemperatureSensors();
    updateTemperatureSensor();

    logPrint("DS18B20 devices found: ");
    logLine(sensors.getDeviceCount());

    // OTA setup
    ArduinoOTA.setHostname("sammybrew-esp32");
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA
    .onStart([]() {

        logLine("OTA update started");

        // VIKTIG:
        // slå av varme før firmwareoppdatering
        requestedHeaterState = false;
        requestedPower = 0.0;
        effectivePower = 0.0;

        requestedHltState = false;
        requestedHltPower = 0.0;
        effectiveHltPower = 0.0;

        requestedPumpState = false;

        setKettleSSR(false);
        setHltSSR(false);
        setCirculationPump(false);

    })

    .onEnd([]() {
        logLine("OTA update finished");
    })

    .onProgress([](
        unsigned int progress,
        unsigned int total
    ) {

        Serial.printf(
            "OTA progress: %u%%\r",
            (progress / (total / 100))
        );
    })

    .onError([](ota_error_t error) {

        Serial.printf(
            "OTA error[%u]\n",
            error
        );
    });

ArduinoOTA.begin();

logLine("OTA ready");
}

// ============================================================================
// Main loop
// ============================================================================

void loop() {
    wsClient.poll();

    ArduinoOTA.handle();

    unsigned long now =
        millis();

    // ------------------------------------------------------------------------
    // Telnet server
    // ------------------------------------------------------------------------
    if (!telnetClient || !telnetClient.connected()) {
        WiFiClient newClient = telnetServer.available();

        if (newClient) {
            telnetClient = newClient;
            logLine("SammyBrew ESP32 Telnet connected");
            logLine();

            scanTemperatureSensors();
        }
    }
    // ------------------------------------------------------------------------
    // Update temperature
    // ------------------------------------------------------------------------

    if (
        now - lastTemperatureRead
        >= TEMPERATURE_READ_INTERVAL_MS
    ) {
        lastTemperatureRead = now;
        updateTemperatureSensor();
    }

    // ------------------------------------------------------------------------
    // WiFi safety
    // ------------------------------------------------------------------------

    if (
        WiFi.status()
        != WL_CONNECTED
    ) {
        websocketConnected = false;
        handshakeComplete = false;
        forceHeaterOff(
            FaultCode::WIFI_LOST
        );
        forceHltHeaterOff(
            FaultCode::WIFI_LOST
        );
        forceCirculationPumpOff(
            FaultCode::WIFI_LOST
        );

        logLine(
            "WiFi lost"
        );

        WiFi.disconnect();
        WiFi.begin(
            WIFI_SSID,
            WIFI_PASSWORD
        );

        delay(1000);
        return;
    }

    // ------------------------------------------------------------------------
    // WebSocket reconnect
    // ------------------------------------------------------------------------

    if (
        !wsClient.available()
    ) {

        websocketConnected = false;
        handshakeComplete = false;

        forceHeaterOff(
            FaultCode::WEBSOCKET_LOST
        );
        forceHltHeaterOff(
            FaultCode::WEBSOCKET_LOST
        );
        forceCirculationPumpOff(
            FaultCode::WEBSOCKET_LOST
        );

        if (
            now - lastReconnectAttempt
            >= RECONNECT_INTERVAL_MS
        ) {
            lastReconnectAttempt = now;
            connectWebSocket();
        }

        delay(10);
        return;
    }

    // ------------------------------------------------------------------------
    // Continuously enforce heater safety
    //
    // Very important:
    // Safety is NOT only checked when a command arrives.
    // ------------------------------------------------------------------------

    updateHeaterSafety();
    updateHltHeaterSafety();
    updateCirculationPumpSafety();
    updateKettleSSR();
    updateHltSSR();

    // ------------------------------------------------------------------------
    // Application ping
    // ------------------------------------------------------------------------

    if (
        now - lastApplicationPing
        >= APPLICATION_PING_INTERVAL_MS
    ) {
        lastApplicationPing = now;
        sendApplicationPing();
    }

    // ------------------------------------------------------------------------
    // Send temperature
    // ------------------------------------------------------------------------

    if (
        now - lastTemperatureSend
        >= TEMPERATURE_SEND_INTERVAL_MS
    ) {
        lastTemperatureSend = now;
        sendTemperature();
    }
    delay(5);
}
