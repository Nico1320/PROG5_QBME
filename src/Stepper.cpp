#include "prog5/Stepper.hpp"
#include <limits>

namespace prog5 {

Stepper::Stepper(StepperIo& io, std::uint32_t halfPeriodUs)
    : io_(io), halfPeriodUs_(halfPeriodUs < 10 ? 10 : halfPeriodUs) {}

void Stepper::begin() {
    io_.begin();
    io_.step(false);
    ready_ = true;
}

bool Stepper::move(std::int32_t steps) {
    if (!ready_) return false;
    if (steps == 0) return true;
    const bool forward = steps > 0;
    // Reject overflow before I/O; steps widen to int64 before subtraction.
    if ((forward && position_ > std::numeric_limits<std::int64_t>::max() - steps) ||
        (!forward && position_ < std::numeric_limits<std::int64_t>::min() - steps)) {
        return false;
    }
    io_.direction(forward);
    io_.delayUs(halfPeriodUs_); // direction setup time; verify driver datasheet
    const auto count = forward ? static_cast<std::int64_t>(steps)
                               : -static_cast<std::int64_t>(steps);
    for (std::int64_t i = 0; i < count; ++i) {
        io_.step(true);
        io_.delayUs(halfPeriodUs_);
        io_.step(false);
        io_.delayUs(halfPeriodUs_);
        position_ += forward ? 1 : -1;
    }
    return true;
}

} // namespace prog5
