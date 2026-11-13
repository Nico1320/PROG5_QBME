#include "prog5/Bmp280.hpp"

#include <cmath>

namespace prog5 {
namespace {
constexpr std::uint8_t chipIdRegister = 0xD0;
constexpr std::uint8_t resetRegister = 0xE0;
constexpr std::uint8_t statusRegister = 0xF3;
constexpr std::uint8_t controlRegister = 0xF4;
constexpr std::uint8_t configRegister = 0xF5;
constexpr std::uint8_t calibrationRegister = 0x88;
constexpr std::uint8_t dataRegister = 0xF7;

std::uint16_t unsigned16(const std::uint8_t* bytes) {
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8);
}

std::int16_t signed16(const std::uint8_t* bytes) {
    const std::uint16_t value = unsigned16(bytes);
    // Avoid an implementation-defined unsigned-to-signed conversion.
    const std::int32_t signedValue = value >= 32768 ? value - 65536L : value;
    return static_cast<std::int16_t>(signedValue);
}

std::uint32_t unsigned20(const std::uint8_t* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 12) |
           (static_cast<std::uint32_t>(bytes[1]) << 4) |
           (static_cast<std::uint32_t>(bytes[2]) >> 4);
}

std::uint8_t code(Oversampling value) {
    return static_cast<std::uint8_t>(value);
}

bool valid(const BmpConfig& config) {
    return code(config.temperature) >= 1 && code(config.temperature) <= 5 &&
           code(config.pressure) >= 1 && code(config.pressure) <= 5;
}

constexpr std::uint8_t sleepMode = 0x00;
constexpr std::uint8_t forcedMode = 0x01;

// ctrl_meas (0xF4) layout: osrs_t[7:5] osrs_p[4:2] mode[1:0], datasheet 4.3.4.
std::uint8_t controlByte(const BmpConfig& config, std::uint8_t mode) {
    return static_cast<std::uint8_t>((code(config.temperature) << 5) |
                                     (code(config.pressure) << 2) | mode);
}
} // namespace

Error decodeCalibration(const std::uint8_t* bytes, std::size_t length,
                        BmpCalibration& calibration) {
    if (!bytes || length != 24) return Error::InvalidArgument;
    BmpCalibration result;
    result.t1 = unsigned16(bytes);
    result.t2 = signed16(bytes + 2);
    result.t3 = signed16(bytes + 4);
    result.p1 = unsigned16(bytes + 6);
    result.p2 = signed16(bytes + 8);
    result.p3 = signed16(bytes + 10);
    result.p4 = signed16(bytes + 12);
    result.p5 = signed16(bytes + 14);
    result.p6 = signed16(bytes + 16);
    result.p7 = signed16(bytes + 18);
    result.p8 = signed16(bytes + 20);
    result.p9 = signed16(bytes + 22);
    if (result.t1 == 0 || result.p1 == 0 ||
        result.t1 == 0xFFFF || result.p1 == 0xFFFF) {
        return Error::InvalidCalibration;
    }
    calibration = result;
    return Error::Ok;
}

Error compensate(const BmpCalibration& c, std::uint32_t rawTemperature,
                 std::uint32_t rawPressure, Measurement& measurement) {
    if (rawTemperature > 0xFFFFF || rawPressure > 0xFFFFF ||
        rawTemperature == 0x80000 || rawPressure == 0x80000) {
        return Error::InvalidData; // 0x80000 means skipped/unavailable.
    }
    if (c.t1 == 0 || c.p1 == 0 || c.t1 == 0xFFFF || c.p1 == 0xFFFF) {
        return Error::InvalidCalibration;
    }

    // Floating-point equations from Bosch BMP280 datasheet rev 1.26,
    // section 8.1. This is a compact implementation, not the Bosch SensorAPI.
    const double delta = rawTemperature / 131072.0 - c.t1 / 8192.0;
    const double temperature1 = (rawTemperature / 16384.0 - c.t1 / 1024.0) * c.t2;
    const double temperature2 = delta * delta * c.t3;
    const std::int32_t tFine = static_cast<std::int32_t>(temperature1 + temperature2);
    Measurement result;
    result.temperatureC = (temperature1 + temperature2) / 5120.0;

    double v1 = tFine / 2.0 - 64000.0;
    double v2 = v1 * v1 * c.p6 / 32768.0;
    v2 += v1 * c.p5 * 2.0;
    v2 = v2 / 4.0 + c.p4 * 65536.0;
    v1 = (c.p3 * v1 * v1 / 524288.0 + c.p2 * v1) / 524288.0;
    v1 = (1.0 + v1 / 32768.0) * c.p1;
    if (v1 == 0.0) return Error::InvalidCalibration;
    double pressure = (1048576.0 - rawPressure - v2 / 4096.0) * 6250.0 / v1;
    v1 = c.p9 * pressure * pressure / 2147483648.0;
    v2 = pressure * c.p8 / 32768.0;
    result.pressurePa = pressure + (v1 + v2 + c.p7) / 16.0;
    if (!std::isfinite(result.temperatureC) || !std::isfinite(result.pressurePa) ||
        result.temperatureC < -40 || result.temperatureC > 85 ||
        result.pressurePa < 30000 || result.pressurePa > 110000) {
        return Error::InvalidData;
    }
    measurement = result;
    return Error::Ok;
}

std::uint32_t measurementTimeUs(const BmpConfig& config) {
    if (!valid(config)) return 0;
    const auto t = 1UL << (code(config.temperature) - 1);
    const auto p = 1UL << (code(config.pressure) - 1);
    // 1.25 + 2.3*T + 2.3*P + 0.575 ms, rounded up to whole ms.
    // Not a formula from datasheet rev 1.26; tests check it bounds every
    // Table 13 maximum (x1/x1: 6.425 >= 6.4 ms, rounded to 7 ms).
    const auto us = 1250UL + 2300UL * t + 2300UL * p + 575UL;
    return static_cast<std::uint32_t>(((us + 999UL) / 1000UL) * 1000UL);
}

Bmp280::Bmp280(I2cBus& bus, Clock& clock, std::uint8_t address, BmpConfig config)
    : bus_(bus), clock_(clock), address_(address), config_(config) {}

bool Bmp280::writeByte(std::uint8_t reg, std::uint8_t value) {
    return bus_.write(address_, reg, &value, 1);
}

// Bounded poll: at most timeoutUs / 1000 + 1 status reads and timeoutUs of delay.
Error Bmp280::waitUntilClear(std::uint8_t mask, std::uint32_t timeoutUs) {
    for (std::uint32_t elapsed = 0; elapsed <= timeoutUs; elapsed += 1000) {
        std::uint8_t status = 0;
        if (!bus_.read(address_, statusRegister, &status, 1)) return Error::BusFailure;
        if ((status & mask) == 0) return Error::Ok;
        if (elapsed == timeoutUs) break;
        clock_.delayUs(1000);
    }
    return Error::Timeout;
}

Error Bmp280::init() {
    initialised_ = false;
    if ((address_ != bmp280AddressSdoLow && address_ != bmp280AddressSdoHigh) ||
        !valid(config_)) {
        return Error::InvalidArgument;
    }
    std::uint8_t id = 0;
    if (!bus_.read(address_, chipIdRegister, &id, 1)) return Error::BusFailure;
    if (id != bmp280ChipId) return Error::WrongChipId; // BME280's 0x60 is unsupported.
    if (!writeByte(resetRegister, 0xB6)) return Error::BusFailure;
    clock_.delayUs(3000); // datasheet start-up time: 2 ms max
    const auto ready = waitUntilClear(0x01, 20000); // im_update: NVM copy done
    if (ready != Error::Ok) return ready;
    std::uint8_t bytes[24] = {};
    if (!bus_.read(address_, calibrationRegister, bytes, sizeof bytes)) {
        return Error::BusFailure;
    }
    const auto decoded = decodeCalibration(bytes, sizeof bytes, calibration_);
    if (decoded != Error::Ok) return decoded;
    if (!writeByte(configRegister, 0)) return Error::BusFailure; // IIR off
    if (!writeByte(controlRegister, controlByte(config_, sleepMode))) {
        return Error::BusFailure;
    }
    initialised_ = true;
    return Error::Ok;
}

Error Bmp280::readForced(Measurement& measurement) {
    if (!initialised_) return Error::NotInitialised;
    if (!writeByte(controlRegister, controlByte(config_, forcedMode))) {
        return Error::BusFailure;
    }
    // Wait before checking status; otherwise a not-yet-started conversion could
    // be mistaken for a completed one.
    clock_.delayUs(measurementTimeUs(config_));
    const auto ready = waitUntilClear(0x09, 100000); // measuring | im_update
    if (ready != Error::Ok) return ready;
    std::uint8_t bytes[6] = {};
    if (!bus_.read(address_, dataRegister, bytes, sizeof bytes)) return Error::BusFailure;
    return compensate(calibration_, unsigned20(bytes + 3), unsigned20(bytes), measurement);
}

} // namespace prog5
