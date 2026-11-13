#pragma once

#include "prog5/Hal.hpp"
#include <string>

namespace prog5 {

// Onboard Linux -> arduino-router -> STM32 Wire.
// Synchronous, one caller; each RPC has a 500 ms socket-I/O deadline.
// A timeout does not cancel MCU work or restore MCU state. A timed-out write
// may have happened; no automatic replay. Requires the matching safe firmware.
class RouterI2cBus final : public I2cBus {
public:
    explicit RouterI2cBus(std::string socketPath = "/var/run/arduino-router.sock");
    ~RouterI2cBus() override;
    RouterI2cBus(const RouterI2cBus&) = delete;
    RouterI2cBus& operator=(const RouterI2cBus&) = delete;
    bool read(std::uint8_t address, std::uint8_t reg,
              std::uint8_t* data, std::size_t length) override;
    bool write(std::uint8_t address, std::uint8_t reg,
               const std::uint8_t* data, std::size_t length) override;
private:
    std::string socketPath_;
};

class SystemClock final : public Clock {
public:
    void delayUs(std::uint32_t duration) override;
};

} // namespace prog5
