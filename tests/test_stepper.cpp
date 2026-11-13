#include "Test.hpp"
#include "prog5/Stepper.hpp"

#include <limits>
#include <vector>

namespace {
struct Event {
    // B = begin, D = direction, S = STEP level, T = requested delay in us.
    char kind;
    std::uint32_t value;
};

struct StopRecording {};

// Records I/O without sleeping.
class RecordingIo final : public prog5::StepperIo {
public:
    std::vector<Event> events;
    unsigned pulses = 0;
    unsigned stopAfter = 0;
    bool high = false;
    void begin() override { events.push_back({'B', 0}); }
    void direction(bool forward) override {
        CHECK(!high);
        events.push_back({'D', forward ? 1u : 0u});
    }
    void step(bool level) override {
        // Interrupt extreme-count tests before billions of pulses.
        if (level && stopAfter && pulses == stopAfter) throw StopRecording{};
        if (level) {
            CHECK(!high);
            ++pulses;
        }
        high = level;
        events.push_back({'S', level ? 1u : 0u});
    }
    void delayUs(std::uint32_t duration) override {
        events.push_back({'T', duration});
    }
};

void checkMove(const RecordingIo& io, std::size_t start, bool forward,
               unsigned count, std::uint32_t halfPeriod) {
    CHECK(io.events.size() == start + 2 + 4 * count);
    CHECK(io.events[start].kind == 'D');
    CHECK(io.events[start].value == (forward ? 1u : 0u));
    CHECK(io.events[start + 1].kind == 'T');
    CHECK(io.events[start + 1].value == halfPeriod);
    for (unsigned i = 0; i < count; ++i) {
        const auto at = start + 2 + 4 * i;
        CHECK(io.events[at].kind == 'S' && io.events[at].value == 1);
        CHECK(io.events[at + 1].kind == 'T' && io.events[at + 1].value == halfPeriod);
        CHECK(io.events[at + 2].kind == 'S' && io.events[at + 2].value == 0);
        CHECK(io.events[at + 3].kind == 'T' && io.events[at + 3].value == halfPeriod);
    }
    CHECK(!io.high);
}
} // namespace

void runStepperTests() {
    RecordingIo io;
    prog5::Stepper motor(io, 250);
    CHECK(io.events.empty());
    CHECK(motor.position() == 0);
    CHECK(!motor.move(1));
    CHECK(!motor.move(0));
    CHECK(!motor.move(std::numeric_limits<std::int32_t>::min()));
    CHECK(io.events.empty());
    motor.begin();
    CHECK(io.events.size() == 2);
    CHECK(io.events[0].kind == 'B');
    CHECK(io.events[1].kind == 'S' && io.events[1].value == 0);
    CHECK(motor.move(0));
    CHECK(io.events.size() == 2);
    CHECK(motor.move(3));
    checkMove(io, 2, true, 3, 250);
    CHECK(io.pulses == 3 && motor.position() == 3);
    auto start = io.events.size();
    CHECK(motor.move(-5));
    checkMove(io, start, false, 5, 250);
    CHECK(io.pulses == 8 && motor.position() == -2);
    start = io.events.size();
    CHECK(motor.move(2));
    checkMove(io, start, true, 2, 250);
    CHECK(io.pulses == 10 && motor.position() == 0);
    CHECK(motor.move(-1));
    motor.begin();
    CHECK(motor.position() == -1); // begin is not homing
    CHECK(!io.high);

    // Clamp boundary, default timing, and maximum duration without real delays.
    for (const auto period : {0u, 9u, 10u, 11u,
                              std::numeric_limits<std::uint32_t>::max()}) {
        RecordingIo timing;
        prog5::Stepper paced(timing, period);
        paced.begin();
        CHECK(paced.move(1));
        checkMove(timing, 2, true, 1, period < 10 ? 10 : period);
    }
    RecordingIo defaults;
    prog5::Stepper defaultMotor(defaults);
    defaultMotor.begin();
    CHECK(defaultMotor.move(-1));
    checkMove(defaults, 2, false, 1, 1000);

    // Exercise signed conversion at both extrema with only three complete steps.
    for (const auto steps : {std::numeric_limits<std::int32_t>::min(),
                             std::numeric_limits<std::int32_t>::max()}) {
        RecordingIo bounded;
        bounded.stopAfter = 3;
        prog5::Stepper largeMove(bounded);
        largeMove.begin();
        bool interrupted = false;
        try {
            largeMove.move(steps);
        } catch (const StopRecording&) {
            interrupted = true;
        }
        CHECK(interrupted);
        checkMove(bounded, 2, steps > 0, 3, 1000);
        CHECK(largeMove.position() == (steps > 0 ? 3 : -3));
    }
}
