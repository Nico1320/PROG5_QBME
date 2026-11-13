#include "MqttPublisher.hpp"
#include <mosquitto.h>

#include <limits>
#include <stdexcept>

namespace prog5 {

MqttRuntime::MqttRuntime() {
    const auto result = mosquitto_lib_init();
    if (result != MOSQ_ERR_SUCCESS) throw std::runtime_error(mosquitto_strerror(result));
}
MqttRuntime::~MqttRuntime() { mosquitto_lib_cleanup(); }

MqttPublisher::MqttPublisher(MqttRuntime&, const std::string& host, int port) {
    if (host.empty() || port < 1 || port > 65535) {
        throw std::invalid_argument("Invalid MQTT host/port");
    }
    client_ = mosquitto_new(nullptr, true, this);
    if (!client_) throw std::runtime_error("Cannot allocate MQTT client");
    mosquitto_connect_callback_set(client_, &MqttPublisher::onConnect);
    mosquitto_disconnect_callback_set(client_, &MqttPublisher::onDisconnect);
    mosquitto_reconnect_delay_set(client_, 1, 10, true);
    auto result = mosquitto_connect_async(client_, host.c_str(), port, 30);
    if (result == MOSQ_ERR_SUCCESS) result = mosquitto_loop_start(client_);
    if (result != MOSQ_ERR_SUCCESS) {
        mosquitto_destroy(client_);
        client_ = nullptr;
        throw std::runtime_error(mosquitto_strerror(result));
    }
}

MqttPublisher::~MqttPublisher() {
    if (client_) {
        mosquitto_disconnect(client_);
        mosquitto_loop_stop(client_, true); // join network thread before destroying context
        mosquitto_destroy(client_);
    }
}

void MqttPublisher::onConnect(mosquitto*, void* context, int result) {
    static_cast<MqttPublisher*>(context)->connected_.store(result == 0);
}
void MqttPublisher::onDisconnect(mosquitto*, void* context, int) {
    static_cast<MqttPublisher*>(context)->connected_.store(false);
}

bool MqttPublisher::publish(const std::string& topic, const std::string& payload) {
    if (!connected_.load() || topic.empty() ||
        payload.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    // true means queued, not delivered. QoS 0, no retain.
    return mosquitto_publish(client_, nullptr, topic.c_str(), static_cast<int>(payload.size()),
                             payload.data(), 0, false) == MOSQ_ERR_SUCCESS;
}

} // namespace prog5
