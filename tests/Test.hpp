#pragma once

#include <cmath>
#include <sstream>
#include <stdexcept>

namespace prog5_test {
inline void check(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        std::ostringstream message;
        message << file << ':' << line << ": " << expression;
        throw std::runtime_error(message.str());
    }
}
inline void near(double actual, double expected, double tolerance,
                 const char* file, int line) {
    check(std::isfinite(actual) && std::abs(actual - expected) <= tolerance,
          "floating-point value outside tolerance", file, line);
}
}

#define CHECK(expression) ::prog5_test::check(static_cast<bool>(expression), #expression, __FILE__, __LINE__)
#define CHECK_NEAR(actual, expected, tolerance) ::prog5_test::near(actual, expected, tolerance, __FILE__, __LINE__)
