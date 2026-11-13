#pragma once

#include "prog5/Hal.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace prog5 {

class Sampler {
public:
    using Callback = std::function<void(Error, const Measurement&)>;
    // sensor and all callback captures must outlive the sampler.
    // start()/stop() are called by the controlling thread, never by a callback.
    // Sensor exceptions become BusFailure callbacks; callback exceptions stop
    // sampling and set callbackFailed(). stop() joins any in-flight read/callback.
    Sampler(EnvironmentSensor& sensor, std::chrono::milliseconds period, Callback callback);
    ~Sampler();
    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;
    void start();
    void stop();
    bool callbackFailed() const { return callbackFailed_.load(); }
private:
    void run();
    EnvironmentSensor& sensor_;
    std::chrono::milliseconds period_;
    Callback callback_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stopRequested_ = false; // protected by mutex_
    std::atomic<bool> callbackFailed_{false};
};

} // namespace prog5
