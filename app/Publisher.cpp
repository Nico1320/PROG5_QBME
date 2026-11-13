#include "Publisher.hpp"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace prog5 {

std::string toJson(const Measurement& measurement) {
    if (!std::isfinite(measurement.temperatureC) ||
        !std::isfinite(measurement.pressurePa)) {
        throw std::invalid_argument("JSON measurements must be finite");
    }
    std::ostringstream output;
    output.imbue(std::locale::classic()); // JSON requires a decimal point.
    output << std::fixed << std::setprecision(2)
           << "{\"temperature_c\":" << measurement.temperatureC
           << ",\"pressure_pa\":" << measurement.pressurePa << '}';
    return output.str();
}

bool ConsolePublisher::publish(const std::string& topic, const std::string& payload) {
    std::cout << topic << ' ' << payload << '\n';
    return static_cast<bool>(std::cout);
}

} // namespace prog5
