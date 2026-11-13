#pragma once

#include "Hal.hpp"

namespace prog5 {

// BMP280 only (datasheet rev 1.26, sections 4.3.1 and 5.2). A BME280 reports 0x60
// and is rejected with Error::WrongChipId.
constexpr std::uint8_t bmp280ChipId = 0x58;
constexpr std::uint8_t bmp280AddressSdoLow = 0x76;  // SDO tied to GND
constexpr std::uint8_t bmp280AddressSdoHigh = 0x77; // SDO tied to VDDIO

enum class Oversampling : std::uint8_t { X1 = 1, X2, X4, X8, X16 };

struct BmpConfig {
    Oversampling temperature = Oversampling::X1;
    Oversampling pressure = Oversampling::X1;
};

struct BmpCalibration {
    std::uint16_t t1 = 0;
    std::int16_t t2 = 0, t3 = 0;
    std::uint16_t p1 = 0;
    std::int16_t p2 = 0, p3 = 0, p4 = 0, p5 = 0;
    std::int16_t p6 = 0, p7 = 0, p8 = 0, p9 = 0;
};

// These functions leave their output parameter unchanged on error.

// bytes: the 24 bytes read from 0x88..0x9F (little-endian dig_T1..dig_P9).
Error decodeCalibration(const std::uint8_t* bytes, std::size_t length,
                        BmpCalibration& calibration);
// Raw values are the unsigned 20-bit ADC readings. Output: degrees C and Pa.
// Results outside the sensor's operating range (-40..85 C, 300..1100 hPa)
// are rejected as Error::InvalidData.
Error compensate(const BmpCalibration& calibration, std::uint32_t rawTemperature,
                 std::uint32_t rawPressure, Measurement& measurement);
// Upper bound for one forced conversion, rounded up to whole ms; 0 if invalid.
std::uint32_t measurementTimeUs(const BmpConfig& config);

class Bmp280 final : public EnvironmentSensor {
public:
    // Non-owning references: bus and clock must outlive this object.
    // Construction does not touch hardware.
    // The address and config are validated by init() (Error::InvalidArgument).
    Bmp280(I2cBus& bus, Clock& clock, std::uint8_t address = bmp280AddressSdoLow,
           BmpConfig config = BmpConfig{});
    Error init() override;
    Error readForced(Measurement& measurement) override;
    bool isInitialised() const { return initialised_; }

private:
    Error waitUntilClear(std::uint8_t mask, std::uint32_t timeoutUs);
    bool writeByte(std::uint8_t reg, std::uint8_t value);

    I2cBus& bus_;
    Clock& clock_;
    std::uint8_t address_;
    BmpConfig config_;
    BmpCalibration calibration_;
    bool initialised_ = false;
};

} // namespace prog5
