#include "FakeSensor.hpp"
#include "Publisher.hpp"
#include "Sampler.hpp"
#include "prog5/Bmp280.hpp"
#ifdef PROG5_HAS_UNO_Q
#include "UnoQPlatform.hpp"
#endif
#ifdef PROG5_HAS_MQTT
#include "MqttPublisher.hpp"
#endif

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
volatile std::sig_atomic_t interrupted = 0;
extern "C" void onSignal(int) { interrupted = 1; }

int parseNumber(const std::string& text, int base) {
    std::size_t used = 0;
    const int result = std::stoi(text, &used, base);
    if (used != text.size()) throw std::invalid_argument("Invalid number: " + text);
    return result;
}
}

int main(int argc, char** argv) {
    try {
        bool fake = false, mqtt = false, mqttFallback = false;
        std::string socketPath = "/var/run/arduino-router.sock", host = "localhost";
        std::string topic = "han/ese/CHANGE-ME/bmp280/state";
        int port = 1883, address = 0x76, seconds = 0, periodMs = 1000;
        for (int i = 1; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "--help") {
                std::cout << "UNO Q station [--fake] [--mqtt]\n"
                             "        [--socket /var/run/arduino-router.sock]\n"
                             "        [--address 0x76] [--host localhost] [--port 1883]\n"
                             "        [--topic han/ese/YOUR-NAME/bmp280/state]\n"
                             "        [--seconds 0] [--period-ms 1000]\n"
                             "0 seconds = run until Ctrl-C. --fake is SIMULATED data.\n"
                             "MQTT setup failure: console only, no retry, failure exit on shutdown.\n";
                return EXIT_SUCCESS;
            }
            if (option == "--fake") { fake = true; continue; }
            if (option == "--mqtt") { mqtt = true; continue; }
            if (++i >= argc) throw std::invalid_argument("Missing value for " + option);
            const std::string value = argv[i];
            if (option == "--socket") socketPath = value;
            else if (option == "--host") host = value;
            else if (option == "--topic") topic = value;
            else if (option == "--address") address = parseNumber(value, 0);
            else if (option == "--port") port = parseNumber(value, 10);
            else if (option == "--seconds") seconds = parseNumber(value, 10);
            else if (option == "--period-ms") periodMs = parseNumber(value, 10);
            else throw std::invalid_argument("Unknown option: " + option);
        }
        if ((address != 0x76 && address != 0x77) || seconds < 0 || periodMs <= 0 ||
            port < 1 || port > 65535 || socketPath.empty() || host.empty() || topic.empty()) {
            throw std::invalid_argument("Invalid option value");
        }

        std::signal(SIGINT, onSignal);
        std::signal(SIGTERM, onSignal);
#ifdef PROG5_HAS_UNO_Q
        prog5::SystemClock clock;
#endif
        std::unique_ptr<prog5::I2cBus> bus;
        std::unique_ptr<prog5::EnvironmentSensor> sensor;
        if (fake) {
            std::cerr << "SIMULATION: FakeSensor, not hardware readings\n";
            sensor.reset(new prog5::FakeSensor);
        } else {
#ifdef PROG5_HAS_UNO_Q
            bus.reset(new prog5::RouterI2cBus(socketPath));
            sensor.reset(new prog5::Bmp280(*bus, clock, static_cast<std::uint8_t>(address)));
#else
            throw std::runtime_error("Rebuild with -DPROG5_WITH_UNO_Q=ON for UNO Q hardware");
#endif
        }
        const auto initialised = sensor->init();
        if (initialised != prog5::Error::Ok) {
            throw std::runtime_error(std::string("Sensor init: ") + prog5::errorName(initialised));
        }
        // Reverse destruction: sampler -> publisher -> MQTT runtime -> sensor -> bus.
#ifdef PROG5_HAS_MQTT
        std::unique_ptr<prog5::MqttRuntime> runtime;
#endif
        std::unique_ptr<prog5::Publisher> publisher;
        if (mqtt) {
            if (topic.find("CHANGE-ME") != std::string::npos) {
                throw std::invalid_argument("Set your own --topic before publishing");
            }
#ifdef PROG5_HAS_MQTT
            runtime.reset(new prog5::MqttRuntime);
            try {
                publisher.reset(new prog5::MqttPublisher(*runtime, host, port));
            } catch (const std::exception& error) {
                // e.g. initial DNS lookup failure. Runtime connection losses
                // are retried by libmosquitto; failed initial setup falls back.
                mqttFallback = true;
                std::cerr << "WARNING: MQTT DISABLED: setup failed: " << error.what()
                          << ". Continuing with CONSOLE ONLY; no MQTT retry."
                             " Restart after fixing the broker. Exit status will be failure.\n";
                publisher.reset(new prog5::ConsolePublisher);
            }
#else
            throw std::runtime_error("Rebuild with -DPROG5_WITH_MQTT=ON");
#endif
        } else publisher.reset(new prog5::ConsolePublisher);
        prog5::Sampler sampler(*sensor, std::chrono::milliseconds(periodMs),
            [&](prog5::Error error, const prog5::Measurement& measurement) {
                if (error != prog5::Error::Ok) {
                    std::cerr << "Sample failed: " << prog5::errorName(error) << '\n';
                } else if (!publisher->publish(topic, prog5::toJson(measurement))) {
                    std::cerr << "Publish unavailable/failed; sample dropped\n";
                }
            });
        sampler.start();
        const auto started = std::chrono::steady_clock::now();
        while (!interrupted && !sampler.callbackFailed()) {
            if (seconds && std::chrono::steady_clock::now() - started >=
                std::chrono::seconds(seconds)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        sampler.stop();
        if (sampler.callbackFailed()) throw std::runtime_error("Measurement callback threw");
        if (mqttFallback) {
            std::cerr << "MQTT verification FAILED: this run used console fallback only.\n";
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
