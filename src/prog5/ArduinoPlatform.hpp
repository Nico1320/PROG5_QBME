#pragma once

#ifdef ARDUINO
#include "Hal.hpp"
#include "Stepper.hpp"
#include <Arduino.h>
#include <Wire.h>

namespace prog5 {

class ArduinoI2cBus final : public I2cBus {
public:
    // Non-owning; the sketch calls wire.begin() in setup(), never construction.
    explicit ArduinoI2cBus(TwoWire& wire) : wire_(wire) {}
    bool read(std::uint8_t address, std::uint8_t reg,
              std::uint8_t* data, std::size_t length) override {
        if (!data || length == 0 || length > 24) return false;
        wire_.beginTransmission(address);
        if (wire_.write(reg) != 1) return false;
        if (wire_.endTransmission(false) != 0) return false;
        const auto received = wire_.requestFrom(address, static_cast<std::uint8_t>(length));
        if (static_cast<std::size_t>(received) != length) {
            while (wire_.available()) wire_.read();
            return false;
        }
        for (std::size_t i = 0; i < length; ++i) {
            if (!wire_.available()) return false;
            data[i] = static_cast<std::uint8_t>(wire_.read());
        }
        return true;
    }
    bool write(std::uint8_t address, std::uint8_t reg,
               const std::uint8_t* data, std::size_t length) override {
        if (!data || length == 0 || length > 24) return false;
        wire_.beginTransmission(address);
        const bool complete = wire_.write(reg) == 1 && wire_.write(data, length) == length;
        const auto result = wire_.endTransmission();
        return complete && result == 0;
    }
private:
    TwoWire& wire_;
};

class ArduinoClock final : public Clock {
public:
    void delayUs(std::uint32_t duration) override {
        // delayMicroseconds() has a limited argument width on some boards.
        if (duration >= 1000) delay(duration / 1000);
        const auto remainder = static_cast<unsigned int>(duration % 1000);
        if (remainder) delayMicroseconds(remainder);
    }
};

class ArduinoStepperIo final : public StepperIo {
public:
    // UNO Q uses 3.3 V logic: connect only to compatible external STEP/DIR inputs.
    // These pin numbers are Arduino header pins, not Linux GPIO line numbers.
    ArduinoStepperIo(std::uint8_t stepPin, std::uint8_t directionPin)
        : stepPin_(stepPin), directionPin_(directionPin) {}
    void begin() override {
        pinMode(stepPin_, OUTPUT);
        pinMode(directionPin_, OUTPUT);
        digitalWrite(stepPin_, LOW);
        digitalWrite(directionPin_, LOW);
    }
    void direction(bool forward) override {
        digitalWrite(directionPin_, forward ? HIGH : LOW);
    }
    void step(bool high) override { digitalWrite(stepPin_, high ? HIGH : LOW); }
    // Software delays and Zephyr scheduling add pulse-timing jitter.
    void delayUs(std::uint32_t duration) override { clock_.delayUs(duration); }
private:
    std::uint8_t stepPin_, directionPin_;
    ArduinoClock clock_;
};

} // namespace prog5
#endif
