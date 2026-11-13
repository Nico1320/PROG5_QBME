#include "Test.hpp"
#include "prog5/MatrixText.hpp"

#include <limits>

void runMatrixTests() {
    using namespace prog5;
    std::uint8_t pixels[matrixPixelCount] = {};
    CHECK(matrixTextWidth(nullptr) == 0);
    CHECK(matrixTextWidth("") == 0);
    CHECK(matrixTextWidth("T") == 3);
    CHECK(matrixTextWidth("DEMO") == 15);
    renderMatrixText("T", 0, pixels);
    CHECK(pixels[matrixWidth] == 1);
    CHECK(pixels[matrixWidth + 1] == 1);
    CHECK(pixels[matrixWidth + 2] == 1);
    CHECK(pixels[2 * matrixWidth] == 0);
    CHECK(pixels[2 * matrixWidth + 1] == 1);
    for (int x = 0; x < matrixWidth; ++x) {
        CHECK(pixels[x] == 0);
        CHECK(pixels[7 * matrixWidth + x] == 0);
    }
    renderMatrixText("T", -2, pixels);
    CHECK(pixels[matrixWidth] == 1);
    CHECK(pixels[matrixWidth + 1] == 0);
    renderMatrixText("T", matrixWidth, pixels);
    for (const auto pixel : pixels) CHECK(pixel == 0);
    renderMatrixText("T", std::numeric_limits<int>::min(), pixels);
    for (const auto pixel : pixels) CHECK(pixel == 0);
    renderMatrixText(nullptr, 0, pixels);
    for (const auto pixel : pixels) CHECK(pixel == 0);
}
