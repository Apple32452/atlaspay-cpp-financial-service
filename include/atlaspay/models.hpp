#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace atlaspay {

enum class AccountStatus { Active, Frozen, Closed };

enum class TransactionType { Deposit, Transfer };

struct Account {
    std::string id;
    std::string owner;
    std::string currency;
    AccountStatus status{AccountStatus::Active};
    std::int64_t created_at_epoch_seconds{};
    std::int64_t balance_minor{};
};

struct Transaction {
    std::string id;
    std::string idempotency_key;
    std::string request_fingerprint;
    TransactionType type{TransactionType::Transfer};
    std::optional<std::string> source_account_id;
    std::optional<std::string> destination_account_id;
    std::int64_t amount_minor{};
    std::string currency;
    std::string status;
    std::string risk_reason;
    std::int64_t created_at_epoch_seconds{};
};

struct RiskDecision {
    bool approved{true};
    std::string reason;
};

std::string to_string(AccountStatus status);
std::string to_string(TransactionType type);
AccountStatus account_status_from_string(const std::string& value);
TransactionType transaction_type_from_string(const std::string& value);

}  // namespace atlaspay
