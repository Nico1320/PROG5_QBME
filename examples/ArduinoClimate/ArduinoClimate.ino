#include <Prog5Climate.h>

// UNO Q: Wire uses SDA D20/PB11 and SCL D21/PB10, not A4/A5.
// For 3.3 V Qwiic, select Wire1 (SDA PD13, SCL PD12) here.
TwoWire& sensorWire = Wire;
prog5::ArduinoI2cBus bus(sensorWire);
prog5::ArduinoClock sensorClock;
// Use 3.3 V power/pull-ups and common GND; CSB high selects I2C.
// Tie SDO low for 0x76 or high for 0x77.
prog5::Bmp280 sensor(bus, sensorClock, 0x76);
bool ready = false;

void setup() {
    Serial.begin(115200);
    sensorWire.begin(); // bus owner starts the bus, not the sensor
    const auto error = sensor.init();
    ready = error == prog5::Error::Ok;
    Serial.println(prog5::errorName(error));
}

void loop() {
    if (!ready) {
        delay(1000);
        return; // check wiring/address and reset after correcting it
    }
    prog5::Measurement measurement;
    const auto error = sensor.readForced(measurement);
    if (error == prog5::Error::Ok) {
        Serial.print("temperature_C=");
        Serial.print(measurement.temperatureC, 2);
        Serial.print(" pressure_Pa=");
        Serial.println(measurement.pressurePa, 2);
    } else {
        Serial.println(prog5::errorName(error));
    }
    delay(1000); // approximately one second plus measurement/printing time
}
