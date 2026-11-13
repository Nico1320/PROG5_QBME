#pragma once

#include <cstdint>

namespace prog5 {

// STEP/DIR driver interface; no direct coil control.
class StepperIo {
public:
    virtual ~StepperIo() = default;
    virtual void begin() = 0;
    virtual void direction(bool forward) = 0;
    virtual void step(bool high) = 0;
    virtual void delayUs(std::uint32_t duration) = 0;
};

class Stepper {
public:
    // io must outlive this object. Construction does not touch hardware.
    // Half-period is clamped to >= 10 us and also used for DIR setup/hold.
    // Check the actual driver's pulse/setup/hold minima; there is no acceleration.
    explicit Stepper(StepperIo& io, std::uint32_t halfPeriodUs = 1000);
    // Sets STEP low; preserves commanded position (does not home the motor).
    void begin();
    // Blocking, single caller only. Positive steps set DIR high.
    // false, without pulses, if not begun or the commanded position would overflow.
    // After begin(), move(0) succeeds without I/O. Success is not motor feedback.
    bool move(std::int32_t steps);
    std::int64_t position() const { return position_; } // commanded steps only

private:
    StepperIo& io_;
    std::uint32_t halfPeriodUs_;
    std::int64_t position_ = 0;
    bool ready_ = false;
};

} // namespace prog5
