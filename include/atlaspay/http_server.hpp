#pragma once

#include "atlaspay/payment_service.hpp"

#include <atomic>
#include <cstdint>
#include <string>

namespace atlaspay {

class HttpServer {
public:
    HttpServer(PaymentService& service, std::string bind_address, std::uint16_t port);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    void run();
    void stop();

private:
    PaymentService& service_;
    std::string bind_address_;
    std::uint16_t port_;
    int server_fd_{-1};
    std::atomic<bool> running_{false};

    void handle_client(int client_fd);
};

}  // namespace atlaspay
