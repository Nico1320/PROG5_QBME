#pragma once

#include "Publisher.hpp"
#include <atomic>

struct mosquitto;

namespace prog5 {

// Must outlive all MQTT clients.
class MqttRuntime {
public:
    MqttRuntime();
    ~MqttRuntime();
    MqttRuntime(const MqttRuntime&) = delete;
    MqttRuntime& operator=(const MqttRuntime&) = delete;
};

class MqttPublisher final : public Publisher {
public:
    MqttPublisher(MqttRuntime& runtime, const std::string& host, int port);
    ~MqttPublisher() override;
    MqttPublisher(const MqttPublisher&) = delete;
    MqttPublisher& operator=(const MqttPublisher&) = delete;
    bool publish(const std::string& topic, const std::string& payload) override;
private:
    static void onConnect(mosquitto*, void* context, int result);
    static void onDisconnect(mosquitto*, void* context, int result);
    mosquitto* client_ = nullptr;
    std::atomic<bool> connected_{false};
};

} // namespace prog5
