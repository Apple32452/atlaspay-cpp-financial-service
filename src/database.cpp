#include "atlaspay/database.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace atlaspay {
namespace {

class Statement {
public:
    Statement(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &statement_, nullptr) != SQLITE_OK) {
            throw std::runtime_error(std::string("sqlite prepare failed: ") + sqlite3_errmsg(db));
        }
    }

    ~Statement() { sqlite3_finalize(statement_); }

    sqlite3_stmt* get() const { return statement_; }

private:
    sqlite3_stmt* statement_{nullptr};
};

void bind_text(sqlite3_stmt* statement, int index, const std::string& value) {
    if (sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        throw std::runtime_error("sqlite text binding failed");
    }
}

void bind_optional_text(sqlite3_stmt* statement, int index, const std::optional<std::string>& value) {
    if (value.has_value()) {
        bind_text(statement, index, *value);
    } else if (sqlite3_bind_null(statement, index) != SQLITE_OK) {
        throw std::runtime_error("sqlite null binding failed");
    }
}

void require_done(sqlite3* db, sqlite3_stmt* statement) {
    if (sqlite3_step(statement) != SQLITE_DONE) {
        throw std::runtime_error(std::string("sqlite statement failed: ") + sqlite3_errmsg(db));
    }
}

std::string column_text(sqlite3_stmt* statement, int index) {
    const auto* value = sqlite3_column_text(statement, index);
    return value == nullptr ? std::string{} : reinterpret_cast<const char*>(value);
}

std::optional<std::string> optional_column_text(sqlite3_stmt* statement, int index) {
    if (sqlite3_column_type(statement, index) == SQLITE_NULL) {
        return std::nullopt;
    }
    return column_text(statement, index);
}

}  // namespace

std::string to_string(AccountStatus status) {
    switch (status) {
        case AccountStatus::Active:
            return "active";
        case AccountStatus::Frozen:
            return "frozen";
        case AccountStatus::Closed:
            return "closed";
    }
    throw std::runtime_error("unknown account status");
}

std::string to_string(TransactionType type) {
    switch (type) {
        case TransactionType::Deposit:
            return "deposit";
        case TransactionType::Transfer:
            return "transfer";
    }
    throw std::runtime_error("unknown transaction type");
}

AccountStatus account_status_from_string(const std::string& value) {
    if (value == "active") {
        return AccountStatus::Active;
    }
    if (value == "frozen") {
        return AccountStatus::Frozen;
    }
    if (value == "closed") {
        return AccountStatus::Closed;
    }
    throw std::runtime_error("invalid account status in database: " + value);
}

TransactionType transaction_type_from_string(const std::string& value) {
    if (value == "deposit") {
        return TransactionType::Deposit;
    }
    if (value == "transfer") {
        return TransactionType::Transfer;
    }
    throw std::runtime_error("invalid transaction type in database: " + value);
}

Database::Database(const std::string& path) {
    if (sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                        nullptr) != SQLITE_OK) {
        const std::string message = db_ == nullptr ? "unknown sqlite error" : sqlite3_errmsg(db_);
        if (db_ != nullptr) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        throw std::runtime_error("failed to open database: " + message);
    }
    sqlite3_busy_timeout(db_, 5000);
}

Database::~Database() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
    }
}

void Database::exec_unlocked(const std::string& sql) const {
    char* error = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
        const std::string message = error == nullptr ? "unknown sqlite error" : error;
        sqlite3_free(error);
        throw std::runtime_error("sqlite exec failed: " + message);
    }
}

void Database::initialize_schema() {
    std::lock_guard lock(mutex_);
    exec_unlocked("PRAGMA foreign_keys = ON;");
    exec_unlocked("PRAGMA journal_mode = WAL;");
    exec_unlocked("PRAGMA synchronous = NORMAL;");
    exec_unlocked(R"SQL(
        CREATE TABLE IF NOT EXISTS accounts (
            id TEXT PRIMARY KEY,
            owner TEXT NOT NULL,
            currency TEXT NOT NULL,
            status TEXT NOT NULL CHECK(status IN ('active', 'frozen', 'closed')),
            created_at INTEGER NOT NULL
        );

        CREATE TABLE IF NOT EXISTS transactions (
            id TEXT PRIMARY KEY,
            idempotency_key TEXT NOT NULL UNIQUE,
            request_fingerprint TEXT NOT NULL,
            type TEXT NOT NULL CHECK(type IN ('deposit', 'transfer')),
            source_account_id TEXT,
            destination_account_id TEXT,
            amount_minor INTEGER NOT NULL CHECK(amount_minor > 0),
            currency TEXT NOT NULL,
            status TEXT NOT NULL,
            risk_reason TEXT NOT NULL DEFAULT '',
            created_at INTEGER NOT NULL,
            FOREIGN KEY(source_account_id) REFERENCES accounts(id),
            FOREIGN KEY(destination_account_id) REFERENCES accounts(id)
        );

        CREATE TABLE IF NOT EXISTS ledger_entries (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            transaction_id TEXT NOT NULL,
            account_id TEXT NOT NULL,
            amount_minor INTEGER NOT NULL,
            created_at INTEGER NOT NULL,
            FOREIGN KEY(transaction_id) REFERENCES transactions(id),
            FOREIGN KEY(account_id) REFERENCES accounts(id)
        );

        CREATE TABLE IF NOT EXISTS audit_events (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            event_type TEXT NOT NULL,
            entity_id TEXT NOT NULL,
            details TEXT NOT NULL,
            created_at INTEGER NOT NULL
        );

        CREATE INDEX IF NOT EXISTS idx_ledger_account ON ledger_entries(account_id, created_at);
        CREATE INDEX IF NOT EXISTS idx_transactions_source_time ON transactions(source_account_id, created_at);
        CREATE INDEX IF NOT EXISTS idx_transactions_destination_time ON transactions(destination_account_id, created_at);
    )SQL");
}

std::int64_t Database::balance_unlocked(const std::string& account_id) const {
    Statement statement(db_, "SELECT COALESCE(SUM(amount_minor), 0) FROM ledger_entries WHERE account_id = ?;");
    bind_text(statement.get(), 1, account_id);
    if (sqlite3_step(statement.get()) != SQLITE_ROW) {
        throw std::runtime_error("failed to calculate account balance");
    }
    return sqlite3_column_int64(statement.get(), 0);
}

std::optional<Account> Database::get_account_unlocked(const std::string& account_id) const {
    Statement statement(db_, "SELECT id, owner, currency, status, created_at FROM accounts WHERE id = ?;");
    bind_text(statement.get(), 1, account_id);
    const int step = sqlite3_step(statement.get());
    if (step == SQLITE_DONE) {
        return std::nullopt;
    }
    if (step != SQLITE_ROW) {
        throw std::runtime_error(std::string("failed to fetch account: ") + sqlite3_errmsg(db_));
    }

    Account account;
    account.id = column_text(statement.get(), 0);
    account.owner = column_text(statement.get(), 1);
    account.currency = column_text(statement.get(), 2);
    account.status = account_status_from_string(column_text(statement.get(), 3));
    account.created_at_epoch_seconds = sqlite3_column_int64(statement.get(), 4);
    account.balance_minor = balance_unlocked(account.id);
    return account;
}

Account Database::create_account(const std::string& id,
                                 const std::string& owner,
                                 const std::string& currency,
                                 std::int64_t created_at) {
    std::lock_guard lock(mutex_);
    Statement statement(db_,
                        "INSERT INTO accounts(id, owner, currency, status, created_at) VALUES(?, ?, ?, 'active', ?);");
    bind_text(statement.get(), 1, id);
    bind_text(statement.get(), 2, owner);
    bind_text(statement.get(), 3, currency);
    sqlite3_bind_int64(statement.get(), 4, created_at);
    require_done(db_, statement.get());

    Statement audit(db_,
                    "INSERT INTO audit_events(event_type, entity_id, details, created_at) VALUES('account_created', ?, ?, ?);");
    bind_text(audit.get(), 1, id);
    bind_text(audit.get(), 2, "owner=" + owner + ",currency=" + currency);
    sqlite3_bind_int64(audit.get(), 3, created_at);
    require_done(db_, audit.get());

    return *get_account_unlocked(id);
}

std::optional<Account> Database::get_account(const std::string& account_id) const {
    std::lock_guard lock(mutex_);
    return get_account_unlocked(account_id);
}

void Database::set_account_status(const std::string& account_id, AccountStatus status, std::int64_t changed_at) {
    std::lock_guard lock(mutex_);
    exec_unlocked("BEGIN IMMEDIATE;");
    try {
        Statement update(db_, "UPDATE accounts SET status = ? WHERE id = ?;");
        bind_text(update.get(), 1, to_string(status));
        bind_text(update.get(), 2, account_id);
        require_done(db_, update.get());
        if (sqlite3_changes(db_) == 0) {
            throw std::runtime_error("account not found");
        }

        Statement audit(db_,
                        "INSERT INTO audit_events(event_type, entity_id, details, created_at) VALUES('account_status_changed', ?, ?, ?);");
        bind_text(audit.get(), 1, account_id);
        bind_text(audit.get(), 2, "status=" + to_string(status));
        sqlite3_bind_int64(audit.get(), 3, changed_at);
        require_done(db_, audit.get());
        exec_unlocked("COMMIT;");
    } catch (...) {
        try {
            exec_unlocked("ROLLBACK;");
        } catch (...) {
        }
        throw;
    }
}

Transaction Database::transaction_from_statement(void* statement_ptr) {
    auto* statement = static_cast<sqlite3_stmt*>(statement_ptr);
    Transaction transaction;
    transaction.id = column_text(statement, 0);
    transaction.idempotency_key = column_text(statement, 1);
    transaction.request_fingerprint = column_text(statement, 2);
    transaction.type = transaction_type_from_string(column_text(statement, 3));
    transaction.source_account_id = optional_column_text(statement, 4);
    transaction.destination_account_id = optional_column_text(statement, 5);
    transaction.amount_minor = sqlite3_column_int64(statement, 6);
    transaction.currency = column_text(statement, 7);
    transaction.status = column_text(statement, 8);
    transaction.risk_reason = column_text(statement, 9);
    transaction.created_at_epoch_seconds = sqlite3_column_int64(statement, 10);
    return transaction;
}

std::optional<Transaction> Database::find_transaction_unlocked(const std::string& key) const {
    Statement statement(db_, R"SQL(
        SELECT id, idempotency_key, request_fingerprint, type, source_account_id,
               destination_account_id, amount_minor, currency, status, risk_reason, created_at
        FROM transactions WHERE idempotency_key = ?;
    )SQL");
    bind_text(statement.get(), 1, key);
    const int step = sqlite3_step(statement.get());
    if (step == SQLITE_DONE) {
        return std::nullopt;
    }
    if (step != SQLITE_ROW) {
        throw std::runtime_error(std::string("failed to find transaction: ") + sqlite3_errmsg(db_));
    }
    return transaction_from_statement(statement.get());
}

std::optional<Transaction> Database::find_transaction_by_idempotency_key(const std::string& key) const {
    std::lock_guard lock(mutex_);
    return find_transaction_unlocked(key);
}

std::vector<Transaction> Database::list_transactions_for_account(const std::string& account_id,
                                                                 std::size_t limit) const {
    std::lock_guard lock(mutex_);
    Statement statement(db_, R"SQL(
        SELECT id, idempotency_key, request_fingerprint, type, source_account_id,
               destination_account_id, amount_minor, currency, status, risk_reason, created_at
        FROM transactions
        WHERE source_account_id = ? OR destination_account_id = ?
        ORDER BY created_at DESC, id DESC
        LIMIT ?;
    )SQL");
    bind_text(statement.get(), 1, account_id);
    bind_text(statement.get(), 2, account_id);
    sqlite3_bind_int64(statement.get(), 3, static_cast<sqlite3_int64>(std::min<std::size_t>(limit, 1000)));

    std::vector<Transaction> transactions;
    while (true) {
        const int step = sqlite3_step(statement.get());
        if (step == SQLITE_DONE) {
            break;
        }
        if (step != SQLITE_ROW) {
            throw std::runtime_error(std::string("failed to list transactions: ") + sqlite3_errmsg(db_));
        }
        transactions.push_back(transaction_from_statement(statement.get()));
    }
    return transactions;
}

std::int64_t Database::outgoing_sum_since(const std::string& account_id,
                                          std::int64_t since_epoch_seconds) const {
    std::lock_guard lock(mutex_);
    Statement statement(db_, R"SQL(
        SELECT COALESCE(SUM(amount_minor), 0)
        FROM transactions
        WHERE source_account_id = ? AND type = 'transfer' AND status = 'completed' AND created_at >= ?;
    )SQL");
    bind_text(statement.get(), 1, account_id);
    sqlite3_bind_int64(statement.get(), 2, since_epoch_seconds);
    if (sqlite3_step(statement.get()) != SQLITE_ROW) {
        throw std::runtime_error("failed to calculate outgoing sum");
    }
    return sqlite3_column_int64(statement.get(), 0);
}

std::int64_t Database::transfer_count_since(const std::string& account_id,
                                            std::int64_t since_epoch_seconds) const {
    std::lock_guard lock(mutex_);
    Statement statement(db_, R"SQL(
        SELECT COUNT(*)
        FROM transactions
        WHERE source_account_id = ? AND type = 'transfer' AND status = 'completed' AND created_at >= ?;
    )SQL");
    bind_text(statement.get(), 1, account_id);
    sqlite3_bind_int64(statement.get(), 2, since_epoch_seconds);
    if (sqlite3_step(statement.get()) != SQLITE_ROW) {
        throw std::runtime_error("failed to calculate transfer count");
    }
    return sqlite3_column_int64(statement.get(), 0);
}

Transaction Database::record_deposit(const Transaction& transaction) {
    std::lock_guard lock(mutex_);
    if (const auto existing = find_transaction_unlocked(transaction.idempotency_key); existing.has_value()) {
        return *existing;
    }

    exec_unlocked("BEGIN IMMEDIATE;");
    try {
        Statement insert_transaction(db_, R"SQL(
            INSERT INTO transactions(
                id, idempotency_key, request_fingerprint, type, source_account_id,
                destination_account_id, amount_minor, currency, status, risk_reason, created_at
            ) VALUES(?, ?, ?, 'deposit', NULL, ?, ?, ?, 'completed', '', ?);
        )SQL");
        bind_text(insert_transaction.get(), 1, transaction.id);
        bind_text(insert_transaction.get(), 2, transaction.idempotency_key);
        bind_text(insert_transaction.get(), 3, transaction.request_fingerprint);
        bind_optional_text(insert_transaction.get(), 4, transaction.destination_account_id);
        sqlite3_bind_int64(insert_transaction.get(), 5, transaction.amount_minor);
        bind_text(insert_transaction.get(), 6, transaction.currency);
        sqlite3_bind_int64(insert_transaction.get(), 7, transaction.created_at_epoch_seconds);
        require_done(db_, insert_transaction.get());

        Statement entry(db_, R"SQL(
            INSERT INTO ledger_entries(transaction_id, account_id, amount_minor, created_at)
            VALUES(?, ?, ?, ?);
        )SQL");
        bind_text(entry.get(), 1, transaction.id);
        bind_text(entry.get(), 2, *transaction.destination_account_id);
        sqlite3_bind_int64(entry.get(), 3, transaction.amount_minor);
        sqlite3_bind_int64(entry.get(), 4, transaction.created_at_epoch_seconds);
        require_done(db_, entry.get());

        Statement audit(db_, R"SQL(
            INSERT INTO audit_events(event_type, entity_id, details, created_at)
            VALUES('deposit_completed', ?, ?, ?);
        )SQL");
        bind_text(audit.get(), 1, transaction.id);
        bind_text(audit.get(), 2, "amount_minor=" + std::to_string(transaction.amount_minor));
        sqlite3_bind_int64(audit.get(), 3, transaction.created_at_epoch_seconds);
        require_done(db_, audit.get());

        exec_unlocked("COMMIT;");
        return transaction;
    } catch (...) {
        try {
            exec_unlocked("ROLLBACK;");
        } catch (...) {
        }
        throw;
    }
}

Transaction Database::record_transfer(const Transaction& transaction) {
    std::lock_guard lock(mutex_);
    if (const auto existing = find_transaction_unlocked(transaction.idempotency_key); existing.has_value()) {
        return *existing;
    }

    exec_unlocked("BEGIN IMMEDIATE;");
    try {
        if (!transaction.source_account_id.has_value() || !transaction.destination_account_id.has_value()) {
            throw std::runtime_error("transfer requires source and destination accounts");
        }
        if (balance_unlocked(*transaction.source_account_id) < transaction.amount_minor) {
            throw std::runtime_error("insufficient funds");
        }

        Statement insert_transaction(db_, R"SQL(
            INSERT INTO transactions(
                id, idempotency_key, request_fingerprint, type, source_account_id,
                destination_account_id, amount_minor, currency, status, risk_reason, created_at
            ) VALUES(?, ?, ?, 'transfer', ?, ?, ?, ?, 'completed', '', ?);
        )SQL");
        bind_text(insert_transaction.get(), 1, transaction.id);
        bind_text(insert_transaction.get(), 2, transaction.idempotency_key);
        bind_text(insert_transaction.get(), 3, transaction.request_fingerprint);
        bind_text(insert_transaction.get(), 4, *transaction.source_account_id);
        bind_text(insert_transaction.get(), 5, *transaction.destination_account_id);
        sqlite3_bind_int64(insert_transaction.get(), 6, transaction.amount_minor);
        bind_text(insert_transaction.get(), 7, transaction.currency);
        sqlite3_bind_int64(insert_transaction.get(), 8, transaction.created_at_epoch_seconds);
        require_done(db_, insert_transaction.get());

        Statement debit(db_, R"SQL(
            INSERT INTO ledger_entries(transaction_id, account_id, amount_minor, created_at)
            VALUES(?, ?, ?, ?);
        )SQL");
        bind_text(debit.get(), 1, transaction.id);
        bind_text(debit.get(), 2, *transaction.source_account_id);
        sqlite3_bind_int64(debit.get(), 3, -transaction.amount_minor);
        sqlite3_bind_int64(debit.get(), 4, transaction.created_at_epoch_seconds);
        require_done(db_, debit.get());

        Statement credit(db_, R"SQL(
            INSERT INTO ledger_entries(transaction_id, account_id, amount_minor, created_at)
            VALUES(?, ?, ?, ?);
        )SQL");
        bind_text(credit.get(), 1, transaction.id);
        bind_text(credit.get(), 2, *transaction.destination_account_id);
        sqlite3_bind_int64(credit.get(), 3, transaction.amount_minor);
        sqlite3_bind_int64(credit.get(), 4, transaction.created_at_epoch_seconds);
        require_done(db_, credit.get());

        Statement audit(db_, R"SQL(
            INSERT INTO audit_events(event_type, entity_id, details, created_at)
            VALUES('transfer_completed', ?, ?, ?);
        )SQL");
        bind_text(audit.get(), 1, transaction.id);
        bind_text(audit.get(), 2, "amount_minor=" + std::to_string(transaction.amount_minor));
        sqlite3_bind_int64(audit.get(), 3, transaction.created_at_epoch_seconds);
        require_done(db_, audit.get());

        exec_unlocked("COMMIT;");
        return transaction;
    } catch (...) {
        try {
            exec_unlocked("ROLLBACK;");
        } catch (...) {
        }
        throw;
    }
}

}  // namespace atlaspay
