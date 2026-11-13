#include <Prog5Climate.h>
#include <prog5/MatrixText.hpp>
#include <Arduino_LED_Matrix.h>
#include <Arduino_RouterBridge.h>
#include <cmath>
#include <cstdio>

// 1 selects labelled synthetic input; set to 0 for an attached BMP280.
// Sensor errors do not fall back to synthetic input.
#ifndef PROG5_MATRIX_DEMO
#define PROG5_MATRIX_DEMO 1
#endif

// Dedicated SDA/SCL headers use Wire; Qwiic uses Wire1.
TwoWire& sensorWire = Wire;
prog5::ArduinoI2cBus bus(sensorWire);
prog5::ArduinoClock sensorClock;
prog5::Bmp280 sensor(bus, sensorClock, prog5::bmp280AddressSdoLow);
Arduino_LED_Matrix matrix;
std::uint8_t pixels[prog5::matrixPixelCount] = {};
prog5::Measurement reading;
prog5::Error lastError = prog5::Error::NotInitialised;
bool sensorReady = false;
bool validReading = false;
char message[64] = "DEMO";
char statusLine[96] = "DEMO STARTING";
int scrollLeft = prog5::matrixWidth;
bool showPressure = false;
bool startupLabel = true;
std::uint32_t lastSampleMs = 0;
std::uint32_t lastFrameMs = 0;

String matrixStatus() {
    // provide_safe runs between loop() invocations, not concurrently with them.
    return String(statusLine);
}

void updateReading() {
#if PROG5_MATRIX_DEMO
    const auto seconds = millis() / 1000UL;
    reading.temperatureC = 20.0 + (seconds % 31) / 10.0;
    reading.pressurePa = 101325.0 + (seconds % 11) * 10.0;
    lastError = prog5::Error::Ok;
    validReading = true;
#else
    if (!sensorReady) {
        lastError = sensor.init();
        sensorReady = lastError == prog5::Error::Ok;
    }
    if (sensorReady) lastError = sensor.readForced(reading);
    validReading = lastError == prog5::Error::Ok;
    if (!validReading) sensorReady = false;
#endif
}

void buildMessage() {
    if (!validReading) {
        std::snprintf(message, sizeof message, "BMP280 ERROR");
        std::snprintf(statusLine, sizeof statusLine, "HARDWARE ERROR %s",
                      prog5::errorName(lastError));
        return;
    }
    const long temperatureTenths = std::lround(reading.temperatureC * 10.0);
    const long magnitude = temperatureTenths < 0 ? -temperatureTenths : temperatureTenths;
    const char* sign = temperatureTenths < 0 ? "-" : "";
    const unsigned long pressureTenthsHpa =
        static_cast<unsigned long>(std::lround(reading.pressurePa / 10.0));
    const char* label = PROG5_MATRIX_DEMO ? "DEMO " : "";
    if (showPressure) {
        std::snprintf(message, sizeof message, "%sP %lu.%lu HPA", label,
                      pressureTenthsHpa / 10, pressureTenthsHpa % 10);
    } else {
        std::snprintf(message, sizeof message, "%sT %s%ld.%ld C", label,
                      sign, magnitude / 10, magnitude % 10);
    }
    std::snprintf(statusLine, sizeof statusLine, "%sT %s%ld.%ld C P %lu.%lu HPA",
                  label, sign, magnitude / 10, magnitude % 10,
                  pressureTenthsHpa / 10, pressureTenthsHpa % 10);
}

void setup() {
    matrix.begin();
    matrix.setGrayscaleBits(1);
#if !PROG5_MATRIX_DEMO
    sensorWire.begin();
#endif
    Bridge.begin();
    Bridge.provide_safe("prog5_matrix_status", matrixStatus);
    updateReading();
    buildMessage();
    prog5::renderMatrixText(PROG5_MATRIX_DEMO ? "D" : "T", 5, pixels);
    matrix.draw(pixels);
    lastFrameMs = millis();
    lastSampleMs = millis();
}

void loop() {
    const std::uint32_t now = millis();
    if (static_cast<std::uint32_t>(now - lastSampleMs) >= 1000) {
        lastSampleMs = now;
        updateReading();
    }
    if (startupLabel && static_cast<std::uint32_t>(now - lastFrameMs) < 600) return;
    startupLabel = false;
    if (static_cast<std::uint32_t>(now - lastFrameMs) < 80) return;
    lastFrameMs = now;
    prog5::renderMatrixText(message, scrollLeft, pixels);
    // Three small bottom-row markers distinguish DEMO mode between labels.
#if PROG5_MATRIX_DEMO
    pixels[7 * prog5::matrixWidth] = 1;
    pixels[7 * prog5::matrixWidth + 2] = 1;
    pixels[7 * prog5::matrixWidth + 4] = 1;
#endif
    matrix.draw(pixels);
    if (--scrollLeft < -prog5::matrixTextWidth(message)) {
        scrollLeft = prog5::matrixWidth;
        showPressure = !showPressure;
        buildMessage(); // freeze a readable value for a complete scroll
    }
}
