#include <Prog5Climate.h>
#include <Arduino_RouterBridge.h>

// This firmware owns Wire; the sensor driver runs on UNO Q onboard Linux.
prog5::ArduinoI2cBus bus(Wire);
constexpr char hexDigits[] = "0123456789ABCDEF";

String readRegisters(int address, int reg, int length) {
    if (address < 0 || address > 127 || reg < 0 || reg > 255 ||
        length < 1 || length > 24) return String("ERR");
    uint8_t bytes[24] = {};
    if (!bus.read(static_cast<uint8_t>(address), static_cast<uint8_t>(reg),
                  bytes, static_cast<size_t>(length))) return String("ERR");
    String result;
    result.reserve(static_cast<unsigned int>(length * 2));
    for (int i = 0; i < length; ++i) {
        result += hexDigits[bytes[i] >> 4];
        result += hexDigits[bytes[i] & 15];
    }
    return result;
}

int decodeNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool writeRegisters(int address, int reg, String hex) {
    if (address < 0 || address > 127 || reg < 0 || reg > 255 ||
        hex.length() < 2 || hex.length() > 48 || hex.length() % 2) return false;
    uint8_t bytes[24] = {};
    const size_t length = hex.length() / 2;
    for (size_t i = 0; i < length; ++i) {
        const int high = decodeNibble(hex[2 * i]), low = decodeNibble(hex[2 * i + 1]);
        if (high < 0 || low < 0) return false;
        bytes[i] = static_cast<uint8_t>(high * 16 + low);
    }
    return bus.write(static_cast<uint8_t>(address), static_cast<uint8_t>(reg), bytes, length);
}

void setup() {
    Wire.begin();
    Bridge.begin();
    // Wire operations execute in loop context, not the background RPC thread.
    Bridge.provide_safe("prog5_i2c_read", readRegisters);
    Bridge.provide_safe("prog5_i2c_write", writeRegisters);
}

void loop() {
    // Return frequently: the core's loop hook services provide_safe callbacks.
}
