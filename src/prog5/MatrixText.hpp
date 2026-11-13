#pragma once

#include <cstdint>

namespace prog5 {

constexpr int matrixWidth = 13;
constexpr int matrixHeight = 8;
constexpr int matrixPixelCount = matrixWidth * matrixHeight;

// Minimal 3x5 uppercase font; at most 95 characters. No Arduino dependency.
int matrixTextWidth(const char* text);
void renderMatrixText(const char* text, int left,
                      std::uint8_t (&pixels)[matrixPixelCount]);

} // namespace prog5
