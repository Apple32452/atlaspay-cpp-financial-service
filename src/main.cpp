#include "atlaspay/database.hpp"
#include "atlaspay/http_server.hpp"
#include "atlaspay/payment_service.hpp"

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

namespace {

std::atomic<atlaspay::HttpServer*> active_server{nullptr};

void handle_signal(int) {
    if (auto* server = active_server.load(); server != nullptr) {
        server->stop();
    }
}

std::string environment_or_default(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value == nullptr || std::string(value).empty() ? fallback : value;
}

}  // namespace

int main() {
    try {
        const std::string database_path = environment_or_default("ATLASPAY_DB_PATH", "atlaspay.db");
        const std::string bind_address = environment_or_default("ATLASPAY_BIND", "127.0.0.1");
        const auto port = static_cast<std::uint16_t>(
            std::stoul(environment_or_default("ATLASPAY_PORT", "8080")));

        atlaspay::Database database(database_path);
        database.initialize_schema();
        atlaspay::PaymentService service(database);
        atlaspay::HttpServer server(service, bind_address, port);

        active_server.store(&server);
        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);
        server.run();
        active_server.store(nullptr);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "fatal: " << error.what() << '\n';
        return 1;
    }
}
