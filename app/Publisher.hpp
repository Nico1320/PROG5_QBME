#pragma once

#include "prog5/Hal.hpp"
#include <string>

namespace prog5 {

std::string toJson(const Measurement& measurement);

class Publisher {
public:
    virtual ~Publisher() = default;
    virtual bool publish(const std::string& topic, const std::string& payload) = 0;
};

class ConsolePublisher final : public Publisher {
public:
    bool publish(const std::string& topic, const std::string& payload) override;
};

} // namespace prog5
