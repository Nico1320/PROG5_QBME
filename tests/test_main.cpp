#include <cstdlib>
#include <exception>
#include <iostream>

void runSensorTests();
void runStepperTests();
void runAppTests();
void runMatrixTests();
#ifdef PROG5_TEST_ROUTER
void runRouterTests();
#endif

int main() {
    try {
        runSensorTests();
        std::cout << "PASS sensor tests\n";
        runStepperTests();
        std::cout << "PASS stepper tests\n";
        runAppTests();
        std::cout << "PASS application tests\n";
        runMatrixTests();
        std::cout << "PASS matrix font/render tests\n";
#ifdef PROG5_TEST_ROUTER
        runRouterTests();
        std::cout << "PASS UNO Q router protocol tests (simulated peer)\n";
#endif
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
