#pragma once

#include "atlaspay/database.hpp"
#include "atlaspay/models.hpp"
#include "atlaspay/risk_engine.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace atlaspay {

class PaymentError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class NotFoundError : public PaymentError {
public:
    using PaymentError::PaymentError;
};

class ConflictError : public PaymentError {
public:
    using PaymentError::PaymentError;
};

class ValidationError : public PaymentError {
public:
    using PaymentError::PaymentError;
};

class RiskRejectedError : public PaymentError {
public:
    using PaymentError::PaymentError;
};

class PaymentService {
public:
    PaymentService(Database& database, RiskLimits limits = {});

    Account create_account(const std::string& owner, const std::string& currency);
    Account get_account(const std::string& account_id) const;
    Account freeze_account(const std::string& account_id);

    Transaction deposit(const std::string& account_id,
                        std::int64_t amount_minor,
                        const std::string& idempotency_key);

    Transaction transfer(const std::string& source_account_id,
                         const std::string& destination_account_id,
                         std::int64_t amount_minor,
                         const std::string& idempotency_key);

    std::vector<Transaction> list_transactions(const std::string& account_id,
                                               std::size_t limit = 100) const;

private:
    Database& database_;
    RiskEngine risk_engine_;
    mutable std::mutex operation_mutex_;

    static std::string generate_id(const std::string& prefix);
    static std::int64_t now_epoch_seconds();
    static std::string normalize_currency(const std::string& currency);
    static void validate_owner(const std::string& owner);
    static void validate_idempotency_key(const std::string& key);
};

}  // namespace atlaspay
