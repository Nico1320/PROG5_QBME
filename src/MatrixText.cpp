#include "prog5/MatrixText.hpp"

namespace prog5 {
namespace {
struct Glyph {
    char character;
    std::uint8_t rows[5];
};

const Glyph font[] = {
    {'0', {7, 5, 5, 5, 7}}, {'1', {2, 6, 2, 2, 7}},
    {'2', {7, 1, 7, 4, 7}}, {'3', {7, 1, 7, 1, 7}},
    {'4', {5, 5, 7, 1, 1}}, {'5', {7, 4, 7, 1, 7}},
    {'6', {7, 4, 7, 5, 7}}, {'7', {7, 1, 2, 2, 2}},
    {'8', {7, 5, 7, 5, 7}}, {'9', {7, 5, 7, 1, 7}},
    {'A', {2, 5, 7, 5, 5}}, {'B', {6, 5, 6, 5, 6}},
    {'C', {7, 4, 4, 4, 7}}, {'D', {6, 5, 5, 5, 6}},
    {'E', {7, 4, 6, 4, 7}}, {'F', {7, 4, 6, 4, 4}},
    {'G', {7, 4, 5, 5, 7}}, {'H', {5, 5, 7, 5, 5}},
    {'I', {7, 2, 2, 2, 7}}, {'J', {1, 1, 1, 5, 7}},
    {'K', {5, 5, 6, 5, 5}}, {'L', {4, 4, 4, 4, 7}},
    {'M', {5, 7, 7, 5, 5}}, {'N', {5, 7, 7, 7, 5}},
    {'O', {7, 5, 5, 5, 7}}, {'P', {7, 5, 7, 4, 4}},
    {'Q', {7, 5, 5, 7, 1}}, {'R', {6, 5, 6, 5, 5}},
    {'S', {7, 4, 7, 1, 7}}, {'T', {7, 2, 2, 2, 2}},
    {'U', {5, 5, 5, 5, 7}}, {'V', {5, 5, 5, 5, 2}},
    {'W', {5, 5, 7, 7, 5}}, {'X', {5, 5, 2, 5, 5}},
    {'Y', {5, 5, 2, 2, 2}}, {'Z', {7, 1, 2, 4, 7}},
    {'.', {0, 0, 0, 0, 2}}, {'-', {0, 0, 7, 0, 0}},
    {' ', {0, 0, 0, 0, 0}}, {'?', {7, 1, 3, 0, 2}}
};

const Glyph& glyphFor(char character) {
    for (const auto& glyph : font) {
        if (glyph.character == character) return glyph;
    }
    return font[sizeof(font) / sizeof(font[0]) - 1];
}
} // namespace

int matrixTextWidth(const char* text) {
    int characters = 0;
    if (text) {
        while (characters < 95 && text[characters]) ++characters;
    }
    return characters ? characters * 4 - 1 : 0;
}

void renderMatrixText(const char* text, int left,
                      std::uint8_t (&pixels)[matrixPixelCount]) {
    for (auto& pixel : pixels) pixel = 0;
    if (!text) return;
    // Reject wholly clipped positions before adding bounded glyph offsets.
    if (left >= matrixWidth || left < -380) return;
    for (int index = 0; index < 95 && text[index]; ++index) {
        const auto& glyph = glyphFor(text[index]);
        for (int row = 0; row < 5; ++row) {
            for (int column = 0; column < 3; ++column) {
                const int x = left + index * 4 + column;
                if (x >= 0 && x < matrixWidth) {
                    // One blank row above and two below the five-pixel font.
                    pixels[(row + 1) * matrixWidth + x] =
                        (glyph.rows[row] & (1U << (2 - column))) ? 1 : 0;
                }
            }
        }
    }
}

} // namespace prog5
