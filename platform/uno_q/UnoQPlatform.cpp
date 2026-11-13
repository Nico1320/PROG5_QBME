#include "UnoQPlatform.hpp"

#include <msgpack.hpp>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <tuple>
#include <unistd.h>
#include <utility>

namespace prog5 {
namespace {
using Deadline = std::chrono::steady_clock::time_point;

class Socket {
public:
    Socket() : fd(::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)) {}
    ~Socket() { if (fd >= 0) ::close(fd); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    int fd;
};

bool waitFor(int fd, short events, Deadline deadline) {
    while (std::chrono::steady_clock::now() < deadline) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        pollfd pfd = {fd, events, 0};
        const auto result = ::poll(&pfd, 1, static_cast<int>(remaining + 1));
        if (result > 0) return (pfd.revents & events) != 0;
        if (result < 0 && errno == EINTR) continue;
        return false;
    }
    return false;
}

// One short-lived connection per request keeps the transport small and avoids
// stale replies after a timeout. The router does not need client registration
// for callers; the STM32 firmware registers the two provided methods.
bool exchange(const std::string& path, const msgpack::sbuffer& request,
              msgpack::object_handle& result) {
    Socket socket;
    sockaddr_un address = {};
    if (socket.fd < 0 || path.empty() || path.find('\0') != std::string::npos ||
        path.size() >= sizeof address.sun_path) return false;
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    if (::connect(socket.fd, reinterpret_cast<sockaddr*>(&address), sizeof address) < 0) {
        if ((errno != EINPROGRESS && errno != EAGAIN && errno != EINTR) ||
            !waitFor(socket.fd, POLLOUT, deadline)) return false;
        int error = 0;
        socklen_t size = sizeof error;
        if (::getsockopt(socket.fd, SOL_SOCKET, SO_ERROR, &error, &size) < 0 || error) return false;
    }
    std::size_t sent = 0;
    while (sent < request.size()) {
        if (!waitFor(socket.fd, POLLOUT, deadline)) return false;
        const auto count = ::send(socket.fd, request.data() + sent, request.size() - sent,
                                  MSG_NOSIGNAL);
        if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (count <= 0) return false;
        sent += static_cast<std::size_t>(count);
    }
    // Limit advertised container sizes too: a tiny malicious ARRAY32 header
    // must not trigger a huge allocation before the wire-byte limit is reached.
    msgpack::unpacker unpacker(nullptr, nullptr, 256,
                              msgpack::unpack_limit(16, 16, 4096, 4096, 4096, 8));
    std::size_t total = 0;
    while (total < 4096) {
        if (!waitFor(socket.fd, POLLIN, deadline)) return false;
        unpacker.reserve_buffer(256);
        const auto count = ::recv(socket.fd, unpacker.buffer(), 256, 0);
        if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (count <= 0) return false;
        unpacker.buffer_consumed(static_cast<std::size_t>(count));
        total += static_cast<std::size_t>(count);
        msgpack::object_handle message;
        while (unpacker.next(message)) {
            const auto& object = message.get();
            if (object.type != msgpack::type::ARRAY || object.via.array.size != 4) continue;
            const auto* fields = object.via.array.ptr;
            if (fields[0].as<int>() != 1 || fields[1].as<int>() != 1) continue;
            if (!fields[2].is_nil()) return false;
            result = msgpack::clone(fields[3]);
            return true;
        }
    }
    return false;
}

int nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
} // namespace

RouterI2cBus::RouterI2cBus(std::string socketPath) : socketPath_(std::move(socketPath)) {}
RouterI2cBus::~RouterI2cBus() = default;

bool RouterI2cBus::read(std::uint8_t address, std::uint8_t reg,
                      std::uint8_t* data, std::size_t length) {
    if (address > 0x7F || !data || length == 0 || length > 24) return false;
    try {
        msgpack::sbuffer request;
        msgpack::pack(request, std::make_tuple(0, 1, "prog5_i2c_read",
            std::make_tuple(static_cast<int>(address), static_cast<int>(reg),
                            static_cast<int>(length))));
        msgpack::object_handle response;
        if (!exchange(socketPath_, request, response)) return false;
        const auto hex = response.get().as<std::string>();
        if (hex.size() != length * 2) return false;
        std::uint8_t decoded[24] = {};
        for (std::size_t i = 0; i < length; ++i) {
            const int high = nibble(hex[2 * i]), low = nibble(hex[2 * i + 1]);
            if (high < 0 || low < 0) return false;
            decoded[i] = static_cast<std::uint8_t>(high * 16 + low);
        }
        std::memcpy(data, decoded, length);
        return true;
    } catch (...) { return false; }
}

bool RouterI2cBus::write(std::uint8_t address, std::uint8_t reg,
                       const std::uint8_t* data, std::size_t length) {
    if (address > 0x7F || !data || length == 0 || length > 24) return false;
    try {
        constexpr char digits[] = "0123456789ABCDEF";
        std::string hex;
        for (std::size_t i = 0; i < length; ++i) {
            hex += digits[data[i] >> 4];
            hex += digits[data[i] & 15];
        }
        msgpack::sbuffer request;
        msgpack::pack(request, std::make_tuple(0, 1, "prog5_i2c_write",
            std::make_tuple(static_cast<int>(address), static_cast<int>(reg), hex)));
        msgpack::object_handle response;
        return exchange(socketPath_, request, response) && response.get().as<bool>();
    } catch (...) { return false; }
}

void SystemClock::delayUs(std::uint32_t duration) {
    std::this_thread::sleep_for(std::chrono::microseconds(duration));
}

} // namespace prog5
