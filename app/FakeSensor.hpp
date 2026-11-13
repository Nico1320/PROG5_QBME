#pragma once

#include "prog5/Hal.hpp"

namespace prog5 {

// Synthetic temperature and pressure input.
class FakeSensor final : public EnvironmentSensor {
public:
    Error init() override {
        count_ = 0;
        ready_ = true;
        return Error::Ok;
    }
    Error readForced(Measurement& measurement) override {
        if (!ready_) return Error::NotInitialised;
        measurement.temperatureC = 20.0 + (count_ % 100) / 10.0;
        measurement.pressurePa = 101325.0;
        ++count_;
        return Error::Ok;
    }
private:
    unsigned count_ = 0;
    bool ready_ = false;
};

} // namespace prog5
