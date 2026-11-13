#include "Sampler.hpp"

#include <stdexcept>
#include <utility>

namespace prog5 {

Sampler::Sampler(EnvironmentSensor& sensor, std::chrono::milliseconds period, Callback callback)
    : sensor_(sensor), period_(period), callback_(std::move(callback)) {
    if (period_.count() <= 0 || !callback_) {
        throw std::invalid_argument("Sampler needs a positive period and a callback");
    }
}

Sampler::~Sampler() { stop(); }

void Sampler::start() {
    if (thread_.joinable()) throw std::logic_error("Sampler already started");
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopRequested_ = false;
    }
    callbackFailed_.store(false);
    thread_ = std::thread(&Sampler::run, this);
}

void Sampler::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopRequested_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void Sampler::run() {
    auto next = std::chrono::steady_clock::now();
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopRequested_) {
        lock.unlock();
        Measurement measurement;
        Error error;
        try {
            error = sensor_.readForced(measurement);
        } catch (...) {
            // A throwing adapter is reported through the same error callback;
            // never let an exception escape a std::thread entry point.
            error = Error::BusFailure;
        }
        try {
            callback_(error, measurement);
        } catch (...) {
            // Do not terminate the process from an exception on this thread.
            // The controller checks callbackFailed() and reports the failure.
            callbackFailed_.store(true);
            return;
        }
        lock.lock();
        next += period_;
        const auto now = std::chrono::steady_clock::now();
        if (next < now) next = now + period_; // skip missed slots, no catch-up burst
        wake_.wait_until(lock, next, [this] { return stopRequested_; });
    }
}

} // namespace prog5
