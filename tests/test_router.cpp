#include "Test.hpp"
#include "UnoQPlatform.hpp"
#include <msgpack.hpp>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <tuple>
#include <unistd.h>
#include <vector>

namespace {
// Simulated router using a local UNIX socket.
class Peer {
public:
    using Handler = std::function<void(int, const msgpack::object&)>;
    explicit Peer(Handler handler) {
        // Fall back to /tmp when the preferred temporary directory is absent.
        const std::string base = ::access("/tmp/opencode", W_OK) == 0
                                     ? "/tmp/opencode" : "/tmp";
        const std::string pattern = base + "/prog5-router-XXXXXX";
        std::vector<char> directory(pattern.begin(), pattern.end());
        directory.push_back('\0');
        const auto created = ::mkdtemp(directory.data());
        CHECK(created != nullptr);
        directory_ = created;
        path_ = directory_ + "/rpc.sock";
        listener_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        CHECK(listener_ >= 0);
        sockaddr_un address = {};
        address.sun_family = AF_UNIX;
        CHECK(path_.size() < sizeof address.sun_path);
        std::memcpy(address.sun_path, path_.c_str(), path_.size() + 1);
        CHECK(::bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0);
        CHECK(::listen(listener_, 1) == 0);
        thread_ = std::thread([this, handler] {
            int client = -1;
            try {
                pollfd ready = {listener_, POLLIN, 0};
                CHECK(::poll(&ready, 1, 2000) == 1);
                client = ::accept(listener_, nullptr, nullptr);
                CHECK(client >= 0);
                msgpack::unpacker unpacker;
                msgpack::object_handle request;
                do {
                    ready = {client, POLLIN, 0};
                    CHECK(::poll(&ready, 1, 2000) == 1);
                    unpacker.reserve_buffer(16);
                    const auto count = ::recv(client, unpacker.buffer(), 16, 0);
                    CHECK(count > 0);
                    unpacker.buffer_consumed(static_cast<std::size_t>(count));
                } while (!unpacker.next(request));
                handler(client, request.get());
            } catch (...) { error_ = std::current_exception(); }
            if (client >= 0) ::close(client);
        });
    }
    ~Peer() {
        if (thread_.joinable()) thread_.join();
        if (listener_ >= 0) ::close(listener_);
        ::unlink(path_.c_str());
        ::rmdir(directory_.c_str());
    }
    const std::string& path() const { return path_; }
    void finish() {
        thread_.join();
        if (error_) std::rethrow_exception(error_);
    }
private:
    std::string directory_, path_;
    int listener_ = -1;
    std::thread thread_;
    std::exception_ptr error_;
};

void sendBytes(int fd, const char* bytes, std::size_t length, bool fragmented = false) {
    std::size_t sent = 0;
    while (sent < length) {
        const auto count = ::send(fd, bytes + sent, fragmented ? 1 : length - sent, MSG_NOSIGNAL);
        if (count < 0 && errno == EINTR) continue;
        CHECK(count > 0);
        sent += static_cast<std::size_t>(count);
        if (fragmented) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
template <typename T>
void reply(int fd, const T& value, bool fragmented = false) {
    msgpack::sbuffer bytes;
    msgpack::pack(bytes, value);
    sendBytes(fd, bytes.data(), bytes.size(), fragmented);
}
const msgpack::object* checkRequest(const msgpack::object& request, const char* method) {
    CHECK(request.type == msgpack::type::ARRAY);
    CHECK(request.via.array.size == 4);
    const auto* fields = request.via.array.ptr;
    CHECK(fields[0].as<int>() == 0);
    CHECK(fields[1].as<int>() == 1);
    CHECK(fields[2].as<std::string>() == method);
    CHECK(fields[3].type == msgpack::type::ARRAY);
    CHECK(fields[3].via.array.size == 3);
    const auto* params = fields[3].via.array.ptr;
    CHECK(params[0].as<int>() == 0x76);
    CHECK(params[1].as<int>() == 0x88);
    return params;
}
}

void runRouterTests() {
    using prog5::RouterI2cBus;
    {
        Peer peer([](int fd, const msgpack::object& request) {
            CHECK(checkRequest(request, "prog5_i2c_read")[2].as<int>() == 3);
            // Notifications and unmatched responses may precede the real reply.
            reply(fd, std::make_tuple(2, "notice", std::make_tuple()));
            reply(fd, std::make_tuple(1, 99, msgpack::type::nil_t{}, "BAD"));
            reply(fd, std::make_tuple(1, 1, msgpack::type::nil_t{}, "00ABFF"), true);
        });
        RouterI2cBus bus(peer.path());
        std::uint8_t bytes[3] = {};
        CHECK(bus.read(0x76, 0x88, bytes, 3));
        CHECK(bytes[0] == 0 && bytes[1] == 0xAB && bytes[2] == 0xFF);
        peer.finish();
    }
    for (bool accepted : {false, true}) {
        Peer peer([accepted](int fd, const msgpack::object& request) {
            CHECK(checkRequest(request, "prog5_i2c_write")[2].as<std::string>() == "00ABFF");
            reply(fd, std::make_tuple(1, 1, msgpack::type::nil_t{}, accepted), true);
        });
        RouterI2cBus bus(peer.path());
        const std::uint8_t bytes[] = {0, 0xAB, 0xFF};
        CHECK(bus.write(0x76, 0x88, bytes, 3) == accepted);
        peer.finish();
    }
    for (const std::string invalid : {"ERR", "0000", "ab", "XZ", ""}) {
        Peer peer([invalid](int fd, const msgpack::object&) {
            reply(fd, std::make_tuple(1, 1, msgpack::type::nil_t{}, invalid));
        });
        RouterI2cBus bus(peer.path());
        std::uint8_t byte = 42;
        CHECK(!bus.read(0x76, 0x88, &byte, 1));
        CHECK(byte == 42);
        peer.finish();
    }
    {
        Peer peer([](int fd, const msgpack::object&) {
            reply(fd, std::make_tuple(1, 1, msgpack::type::nil_t{}, "00XZ"));
        });
        RouterI2cBus bus(peer.path());
        std::uint8_t bytes[] = {42, 43};
        CHECK(!bus.read(0x76, 0x88, bytes, 2));
        CHECK(bytes[0] == 42 && bytes[1] == 43); // decode atomically
        peer.finish();
    }
    for (int mode = 0; mode < 6; ++mode) {
        Peer peer([mode](int fd, const msgpack::object&) {
            if (mode == 0) reply(fd, std::make_tuple(1, 1, "method unavailable", msgpack::type::nil_t{}));
            if (mode == 1) reply(fd, std::make_tuple(1, 1, msgpack::type::nil_t{}, 123)); // wrong result type
            if (mode == 2) { // truncated response then disconnect
                const char partial[] = {char(0x94), 1, 1};
                sendBytes(fd, partial, sizeof partial);
            }
            if (mode == 3) { // absurd ARRAY32 length must not allocate gigabytes
                const char huge[] = {char(0xDD), char(0xFF), char(0xFF), char(0xFF), char(0xFF)};
                sendBytes(fd, huge, sizeof huge);
            }
            if (mode == 4) std::this_thread::sleep_for(std::chrono::milliseconds(650));
            // mode 5: immediate disconnect
        });
        RouterI2cBus bus(peer.path());
        std::uint8_t byte = 42;
        const auto before = std::chrono::steady_clock::now();
        CHECK(!bus.read(0x76, 0x88, &byte, 1));
        CHECK(std::chrono::steady_clock::now() - before < std::chrono::seconds(2));
        CHECK(byte == 42);
        peer.finish();
    }
    RouterI2cBus absent("/tmp/opencode/nonexistent-prog5-router/socket");
    std::uint8_t byte = 0;
    CHECK(!absent.read(0x76, 0x88, &byte, 1));
    CHECK(!absent.read(0x80, 0x88, &byte, 1));
    CHECK(!absent.read(0x76, 0x88, nullptr, 1));
    CHECK(!absent.read(0x76, 0x88, &byte, 0));
    CHECK(!absent.read(0x76, 0x88, &byte, 25));
    CHECK(!absent.write(0x76, 0x88, nullptr, 1));
    CHECK(!absent.write(0x76, 0x88, &byte, 25));
    RouterI2cBus embedded(std::string("abc\0def", 7));
    CHECK(!embedded.read(0x76, 0x88, &byte, 1));
}
