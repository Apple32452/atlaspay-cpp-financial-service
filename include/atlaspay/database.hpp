#pragma once

#include "atlaspay/models.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace atlaspay {

class Database {
public:
    explicit Database(const std::string& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    void initialize_schema();

    Account create_account(const std::string& id,
                           const std::string& owner,
                           const std::string& currency,
                           std::int64_t created_at);
    std::optional<Account> get_account(const std::string& account_id) const;
    void set_account_status(const std::string& account_id, AccountStatus status, std::int64_t changed_at);

    std::optional<Transaction> find_transaction_by_idempotency_key(const std::string& key) const;
    std::vector<Transaction> list_transactions_for_account(const std::string& account_id,
                                                           std::size_t limit) const;

    std::int64_t outgoing_sum_since(const std::string& account_id, std::int64_t since_epoch_seconds) const;
    std::int64_t transfer_count_since(const std::string& account_id, std::int64_t since_epoch_seconds) const;

    Transaction record_deposit(const Transaction& transaction);
    Transaction record_transfer(const Transaction& transaction);

private:
    sqlite3* db_{nullptr};
    mutable std::mutex mutex_;

    void exec_unlocked(const std::string& sql) const;
    std::int64_t balance_unlocked(const std::string& account_id) const;
    std::optional<Account> get_account_unlocked(const std::string& account_id) const;
    std::optional<Transaction> find_transaction_unlocked(const std::string& key) const;
    static Transaction transaction_from_statement(void* statement_ptr);
};

}  // namespace atlaspay
