#include "Test.hpp"
#include "FakeSensor.hpp"
#include "Publisher.hpp"
#include "Sampler.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <locale>
#include <mutex>
#include <stdexcept>

namespace {
struct CommaPunctuation : std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
};
struct LocaleGuard {
    std::locale previous = std::locale();
    ~LocaleGuard() { std::locale::global(previous); }
};
class FailingSensor : public prog5::EnvironmentSensor {
public:
    prog5::Error init() override { return prog5::Error::Ok; }
    prog5::Error readForced(prog5::Measurement&) override {
        if (throws) throw std::runtime_error("simulated adapter exception");
        return prog5::Error::Timeout;
    }
    bool throws = false;
};
}

void runAppTests() {
    using namespace prog5;
    {
        LocaleGuard guard;
        std::locale::global(std::locale(std::locale::classic(), new CommaPunctuation));
        Measurement value;
        value.temperatureC = -1.25;
        value.pressurePa = 101325.5;
        CHECK(toJson(value) == "{\"temperature_c\":-1.25,\"pressure_pa\":101325.50}");
        for (double invalid : {std::numeric_limits<double>::quiet_NaN(),
                               std::numeric_limits<double>::infinity()}) {
            value.temperatureC = invalid;
            bool threw = false;
            try { toJson(value); } catch (const std::invalid_argument&) { threw = true; }
            CHECK(threw);
            value.temperatureC = 20;
            value.pressurePa = invalid;
            threw = false;
            try { toJson(value); } catch (const std::invalid_argument&) { threw = true; }
            CHECK(threw);
        }
    }
    FakeSensor fake;
    Measurement value;
    value.temperatureC = 99;
    CHECK(fake.readForced(value) == Error::NotInitialised);
    CHECK_NEAR(value.temperatureC, 99, 0);
    CHECK(fake.init() == Error::Ok);
    CHECK(fake.readForced(value) == Error::Ok);
    CHECK_NEAR(value.temperatureC, 20, 0);
    CHECK_NEAR(value.pressurePa, 101325, 0);
    CHECK(fake.readForced(value) == Error::Ok);
    CHECK_NEAR(value.temperatureC, 20.1, 0.00001);
    CHECK(fake.init() == Error::Ok);
    CHECK(fake.readForced(value) == Error::Ok);
    CHECK_NEAR(value.temperatureC, 20, 0);

    bool threw = false;
    try { Sampler invalid(fake, std::chrono::milliseconds(0), [](Error, const Measurement&) {}); }
    catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { Sampler invalid(fake, std::chrono::milliseconds(1), Sampler::Callback{}); }
    catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);

    std::mutex mutex;
    std::condition_variable wake;
    int calls = 0;
    Sampler sampler(fake, std::chrono::milliseconds(10000),
        [&](Error error, const Measurement&) {
            CHECK(error == Error::Ok);
            std::lock_guard<std::mutex> lock(mutex);
            ++calls;
            wake.notify_all();
        });
    sampler.stop(); // safe before start and repeated
    for (int expected = 1; expected <= 2; ++expected) {
        sampler.start();
        bool duplicateRejected = false;
        try { sampler.start(); } catch (const std::logic_error&) { duplicateRejected = true; }
        CHECK(duplicateRejected);
        {
            std::unique_lock<std::mutex> lock(mutex);
            CHECK(wake.wait_for(lock, std::chrono::seconds(2),
                                [&] { return calls >= expected; }));
        }
        const auto before = std::chrono::steady_clock::now();
        sampler.stop(); // must interrupt the ten-second period wait
        CHECK(std::chrono::steady_clock::now() - before < std::chrono::seconds(1));
        CHECK(!sampler.callbackFailed());
    }
    sampler.stop();
    CHECK(calls == 2);

    FailingSensor failure;
    for (bool throws : {false, true}) {
        failure.throws = throws;
        std::atomic<int> errors{0};
        Sampler failing(failure, std::chrono::milliseconds(1),
            [&](Error error, const Measurement&) {
                CHECK(error == (throws ? Error::BusFailure : Error::Timeout));
                errors.fetch_add(1);
                wake.notify_all();
            });
        failing.start();
        {
            std::unique_lock<std::mutex> lock(mutex);
            // Short waits also avoid depending on notification timing.
            for (int i = 0; i < 200 && errors.load() == 0; ++i)
                wake.wait_for(lock, std::chrono::milliseconds(10));
        }
        failing.stop();
        CHECK(errors.load() > 0);
        CHECK(!failing.callbackFailed());
    }
    std::atomic<int> attempts{0};
    Sampler callbackFailure(fake, std::chrono::milliseconds(1),
        [&](Error, const Measurement&) {
            if (attempts.fetch_add(1) == 0) throw std::runtime_error("simulated callback");
        });
    callbackFailure.start();
    for (int i = 0; i < 200 && !callbackFailure.callbackFailed(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    callbackFailure.stop();
    CHECK(callbackFailure.callbackFailed());
    callbackFailure.start();
    for (int i = 0; i < 200 && attempts.load() < 2; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    callbackFailure.stop();
    CHECK(!callbackFailure.callbackFailed());
    CHECK(attempts.load() >= 2);
}
