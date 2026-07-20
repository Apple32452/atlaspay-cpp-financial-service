#include "atlaspay/http_server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace atlaspay {
namespace {

struct HttpRequest {
    std::string method;
    std::string path;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string json_escape(const std::string& value) {
    std::ostringstream stream;
    for (const char character : value) {
        switch (character) {
            case '\\':
                stream << "\\\\";
                break;
            case '"':
                stream << "\\\"";
                break;
            case '\n':
                stream << "\\n";
                break;
            case '\r':
                stream << "\\r";
                break;
            case '\t':
                stream << "\\t";
                break;
            default:
                stream << character;
        }
    }
    return stream.str();
}

std::unordered_map<std::string, std::string> parse_flat_json(const std::string& input) {
    std::unordered_map<std::string, std::string> result;
    std::size_t position = 0;

    auto skip_space = [&]() {
        while (position < input.size() && std::isspace(static_cast<unsigned char>(input[position])) != 0) {
            ++position;
        }
    };

    auto parse_string = [&]() -> std::string {
        skip_space();
        if (position >= input.size() || input[position] != '"') {
            throw ValidationError("expected JSON string");
        }
        ++position;
        std::string value;
        while (position < input.size()) {
            const char character = input[position++];
            if (character == '"') {
                return value;
            }
            if (character == '\\') {
                if (position >= input.size()) {
                    throw ValidationError("invalid JSON escape");
                }
                const char escaped = input[position++];
                switch (escaped) {
                    case '"':
                    case '\\':
                    case '/':
                        value.push_back(escaped);
                        break;
                    case 'n':
                        value.push_back('\n');
                        break;
                    case 'r':
                        value.push_back('\r');
                        break;
                    case 't':
                        value.push_back('\t');
                        break;
                    default:
                        throw ValidationError("unsupported JSON escape");
                }
            } else {
                value.push_back(character);
            }
        }
        throw ValidationError("unterminated JSON string");
    };

    skip_space();
    if (position >= input.size() || input[position] != '{') {
        throw ValidationError("request body must be a JSON object");
    }
    ++position;

    while (true) {
        skip_space();
        if (position < input.size() && input[position] == '}') {
            ++position;
            break;
        }
        const std::string key = parse_string();
        skip_space();
        if (position >= input.size() || input[position] != ':') {
            throw ValidationError("expected ':' after JSON key");
        }
        ++position;
        skip_space();

        std::string value;
        if (position < input.size() && input[position] == '"') {
            value = parse_string();
        } else {
            const std::size_t start = position;
            while (position < input.size() && input[position] != ',' && input[position] != '}') {
                ++position;
            }
            value = trim(input.substr(start, position - start));
            if (value.empty()) {
                throw ValidationError("missing JSON value");
            }
        }
        result[key] = value;

        skip_space();
        if (position < input.size() && input[position] == ',') {
            ++position;
            continue;
        }
        if (position < input.size() && input[position] == '}') {
            ++position;
            break;
        }
        throw ValidationError("expected ',' or '}' in JSON object");
    }

    skip_space();
    if (position != input.size()) {
        throw ValidationError("unexpected content after JSON object");
    }
    return result;
}

const std::string& required_field(const std::unordered_map<std::string, std::string>& fields,
                                  const std::string& key) {
    const auto iterator = fields.find(key);
    if (iterator == fields.end() || iterator->second.empty()) {
        throw ValidationError("missing required field: " + key);
    }
    return iterator->second;
}

std::int64_t parse_int64(const std::string& value, const std::string& field_name) {
    std::int64_t parsed{};
    const auto [pointer, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || pointer != value.data() + value.size()) {
        throw ValidationError(field_name + " must be an integer");
    }
    return parsed;
}

std::string account_json(const Account& account) {
    return "{\"id\":\"" + json_escape(account.id) + "\",\"owner\":\"" +
           json_escape(account.owner) + "\",\"currency\":\"" + json_escape(account.currency) +
           "\",\"status\":\"" + to_string(account.status) + "\",\"balance_minor\":" +
           std::to_string(account.balance_minor) + ",\"created_at\":" +
           std::to_string(account.created_at_epoch_seconds) + "}";
}

std::string optional_json_string(const std::optional<std::string>& value) {
    return value.has_value() ? "\"" + json_escape(*value) + "\"" : "null";
}

std::string transaction_json(const Transaction& transaction) {
    return "{\"id\":\"" + json_escape(transaction.id) + "\",\"type\":\"" +
           to_string(transaction.type) + "\",\"source_account_id\":" +
           optional_json_string(transaction.source_account_id) + ",\"destination_account_id\":" +
           optional_json_string(transaction.destination_account_id) + ",\"amount_minor\":" +
           std::to_string(transaction.amount_minor) + ",\"currency\":\"" +
           json_escape(transaction.currency) + "\",\"status\":\"" + json_escape(transaction.status) +
           "\",\"created_at\":" + std::to_string(transaction.created_at_epoch_seconds) + "}";
}

std::string make_response(int status, const std::string& reason, const std::string& body) {
    std::ostringstream response;
    response << "HTTP/1.1 " << status << ' ' << reason << "\r\n"
             << "Content-Type: application/json\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n"
             << "X-Content-Type-Options: nosniff\r\n\r\n"
             << body;
    return response.str();
}

void send_all(int socket_fd, const std::string& response) {
    std::size_t sent = 0;
    while (sent < response.size()) {
        const ssize_t count = ::send(socket_fd, response.data() + sent, response.size() - sent, MSG_NOSIGNAL);
        if (count <= 0) {
            return;
        }
        sent += static_cast<std::size_t>(count);
    }
}

HttpRequest read_request(int client_fd) {
    constexpr std::size_t max_request_size = 1U << 20U;
    std::string raw;
    raw.reserve(4096);
    char buffer[4096];
    std::size_t header_end = std::string::npos;
    std::size_t expected_size = 0;

    while (raw.size() < max_request_size) {
        const ssize_t received = ::recv(client_fd, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            break;
        }
        raw.append(buffer, static_cast<std::size_t>(received));
        if (header_end == std::string::npos) {
            header_end = raw.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                expected_size = header_end + 4;
                const std::string headers = lower(raw.substr(0, header_end));
                const std::string marker = "content-length:";
                const auto marker_position = headers.find(marker);
                if (marker_position != std::string::npos) {
                    const auto line_end = headers.find("\r\n", marker_position);
                    const auto value = trim(headers.substr(marker_position + marker.size(),
                                                           line_end - marker_position - marker.size()));
                    expected_size += static_cast<std::size_t>(parse_int64(value, "Content-Length"));
                }
            }
        }
        if (header_end != std::string::npos && raw.size() >= expected_size) {
            break;
        }
    }

    if (raw.size() >= max_request_size) {
        throw ValidationError("request too large");
    }
    header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        throw ValidationError("malformed HTTP request");
    }

    std::istringstream header_stream(raw.substr(0, header_end));
    HttpRequest request;
    std::string request_line;
    std::getline(header_stream, request_line);
    request_line = trim(request_line);
    std::istringstream request_line_stream(request_line);
    std::string version;
    request_line_stream >> request.method >> request.path >> version;
    if (request.method.empty() || request.path.empty() || version.rfind("HTTP/", 0) != 0) {
        throw ValidationError("malformed HTTP request line");
    }

    std::string line;
    while (std::getline(header_stream, line)) {
        line = trim(line);
        const auto separator = line.find(':');
        if (separator != std::string::npos) {
            request.headers[lower(trim(line.substr(0, separator)))] = trim(line.substr(separator + 1));
        }
    }
    request.body = raw.substr(header_end + 4);
    return request;
}

std::pair<int, std::string> error_status(const std::exception& error) {
    if (dynamic_cast<const NotFoundError*>(&error) != nullptr) {
        return {404, "Not Found"};
    }
    if (dynamic_cast<const ConflictError*>(&error) != nullptr) {
        return {409, "Conflict"};
    }
    if (dynamic_cast<const RiskRejectedError*>(&error) != nullptr) {
        return {422, "Unprocessable Entity"};
    }
    if (dynamic_cast<const ValidationError*>(&error) != nullptr) {
        return {400, "Bad Request"};
    }
    return {500, "Internal Server Error"};
}

std::vector<std::string> split_path(const std::string& path) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start < path.size()) {
        while (start < path.size() && path[start] == '/') {
            ++start;
        }
        if (start >= path.size()) {
            break;
        }
        const auto end = path.find('/', start);
        parts.push_back(path.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return parts;
}

}  // namespace

HttpServer::HttpServer(PaymentService& service, std::string bind_address, std::uint16_t port)
    : service_(service), bind_address_(std::move(bind_address)), port_(port) {}

HttpServer::~HttpServer() { stop(); }

void HttpServer::run() {
    server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        throw std::runtime_error("socket creation failed: " + std::string(std::strerror(errno)));
    }

    int enabled = 1;
    ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port_);
    if (::inet_pton(AF_INET, bind_address_.c_str(), &address.sin_addr) != 1) {
        throw std::runtime_error("invalid bind address: " + bind_address_);
    }

    if (::bind(server_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        throw std::runtime_error("bind failed: " + std::string(std::strerror(errno)));
    }
    if (::listen(server_fd_, 128) < 0) {
        throw std::runtime_error("listen failed: " + std::string(std::strerror(errno)));
    }

    running_.store(true);
    std::cout << "AtlasPay listening on http://" << bind_address_ << ':' << port_ << '\n';

    while (running_.load()) {
        const int client_fd = ::accept(server_fd_, nullptr, nullptr);
        if (client_fd < 0) {
            if (!running_.load() || errno == EINTR || errno == EBADF) {
                continue;
            }
            std::cerr << "accept failed: " << std::strerror(errno) << '\n';
            continue;
        }
        std::thread(&HttpServer::handle_client, this, client_fd).detach();
    }
}

void HttpServer::stop() {
    running_.store(false);
    if (server_fd_ >= 0) {
        ::shutdown(server_fd_, SHUT_RDWR);
        ::close(server_fd_);
        server_fd_ = -1;
    }
}

void HttpServer::handle_client(int client_fd) {
    try {
        const HttpRequest request = read_request(client_fd);
        std::string body;
        int status = 200;
        std::string reason = "OK";

        if (request.method == "GET" && request.path == "/healthz") {
            body = "{\"status\":\"ok\"}";
        } else if (request.method == "POST" && request.path == "/v1/accounts") {
            const auto fields = parse_flat_json(request.body);
            const Account account = service_.create_account(required_field(fields, "owner"),
                                                            required_field(fields, "currency"));
            status = 201;
            reason = "Created";
            body = account_json(account);
        } else if (request.method == "POST" && request.path == "/v1/deposits") {
            const auto fields = parse_flat_json(request.body);
            const Transaction transaction = service_.deposit(
                required_field(fields, "account_id"),
                parse_int64(required_field(fields, "amount_minor"), "amount_minor"),
                required_field(fields, "idempotency_key"));
            status = 201;
            reason = "Created";
            body = transaction_json(transaction);
        } else if (request.method == "POST" && request.path == "/v1/transfers") {
            const auto fields = parse_flat_json(request.body);
            const Transaction transaction = service_.transfer(
                required_field(fields, "source_account_id"),
                required_field(fields, "destination_account_id"),
                parse_int64(required_field(fields, "amount_minor"), "amount_minor"),
                required_field(fields, "idempotency_key"));
            status = 201;
            reason = "Created";
            body = transaction_json(transaction);
        } else {
            const auto parts = split_path(request.path);
            if (request.method == "GET" && parts.size() == 3 && parts[0] == "v1" &&
                parts[1] == "accounts") {
                body = account_json(service_.get_account(parts[2]));
            } else if (request.method == "POST" && parts.size() == 4 && parts[0] == "v1" &&
                       parts[1] == "accounts" && parts[3] == "freeze") {
                body = account_json(service_.freeze_account(parts[2]));
            } else if (request.method == "GET" && parts.size() == 4 && parts[0] == "v1" &&
                       parts[1] == "accounts" && parts[3] == "transactions") {
                const auto transactions = service_.list_transactions(parts[2]);
                std::ostringstream stream;
                stream << "{\"transactions\":[";
                for (std::size_t index = 0; index < transactions.size(); ++index) {
                    if (index != 0) {
                        stream << ',';
                    }
                    stream << transaction_json(transactions[index]);
                }
                stream << "]}";
                body = stream.str();
            } else {
                status = 404;
                reason = "Not Found";
                body = "{\"error\":\"route not found\"}";
            }
        }
        send_all(client_fd, make_response(status, reason, body));
    } catch (const std::exception& error) {
        const auto [status, reason] = error_status(error);
        const std::string body = "{\"error\":\"" + json_escape(error.what()) + "\"}";
        send_all(client_fd, make_response(status, reason, body));
    }
    ::shutdown(client_fd, SHUT_RDWR);
    ::close(client_fd);
}

}  // namespace atlaspay
