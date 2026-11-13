#pragma once

#include <cstddef>
#include <cstdint>

namespace prog5 {

enum class Error {
    Ok,
    InvalidArgument,
    BusFailure,
    WrongChipId,
    InvalidCalibration,
    NotInitialised,
    Timeout,
    InvalidData
};

const char* errorName(Error error);

struct Measurement {
    double temperatureC = 0;
    double pressurePa = 0; // Pa, not hPa.
};

// A successful operation transfers all requested bytes. Implementations must
// report short transfers as failures, and reads must use a repeated START.
class I2cBus {
public:
    virtual ~I2cBus() = default;
    virtual bool read(std::uint8_t address, std::uint8_t reg,
                      std::uint8_t* data, std::size_t length) = 0;
    virtual bool write(std::uint8_t address, std::uint8_t reg,
                       const std::uint8_t* data, std::size_t length) = 0;
};

class Clock {
public:
    virtual ~Clock() = default;
    virtual void delayUs(std::uint32_t duration) = 0;
};

class EnvironmentSensor {
public:
    virtual ~EnvironmentSensor() = default;
    virtual Error init() = 0;
    // On error, leave the caller's previous measurement untouched.
    // One calling thread only; implementations need not be thread-safe.
    virtual Error readForced(Measurement& measurement) = 0;
};

} // namespace prog5
