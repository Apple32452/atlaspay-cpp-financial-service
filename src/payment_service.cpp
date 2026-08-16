#include "atlaspay/payment_service.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

namespace atlaspay {
namespace {

std::string fingerprint(const std::string& operation,
                        const std::string& first,
                        const std::string& second,
                        std::int64_t amount) {
    return operation + "|" + first + "|" + second + "|" + std::to_string(amount);
}

}  // namespace

PaymentService::PaymentService(Database& database, RiskLimits limits)
    : database_(database), risk_engine_(database, limits) {}

std::int64_t PaymentService::now_epoch_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string PaymentService::generate_id(const std::string& prefix) {
    thread_local std::mt19937_64 generator(std::random_device{}());
    std::uniform_int_distribution<std::uint64_t> distribution;

    std::ostringstream stream;
    stream << prefix
           << '_'
           << std::hex
           << std::setfill('0')
           << std::setw(16)
           << distribution(generator)
           << std::setw(16)
           << distribution(generator);

    return stream.str();
}

std::string PaymentService::normalize_currency(const std::string& currency) {
    if (currency.size() != 3) {
        throw ValidationError(
            "currency must be a three-letter ISO-style code");
    }

    std::string normalized = currency;

    std::transform(
        normalized.begin(),
        normalized.end(),
        normalized.begin(),
        [](unsigned char value) {
            return static_cast<char>(std::toupper(value));
        });

    if (!std::all_of(
            normalized.begin(),
            normalized.end(),
            [](unsigned char value) {
                return std::isalpha(value) != 0;
            })) {
        throw ValidationError(
            "currency must contain only letters");
    }

    return normalized;
}

void PaymentService::validate_owner(const std::string& owner) {
    const bool contains_non_whitespace =
        std::any_of(
            owner.begin(),
            owner.end(),
            [](unsigned char character) {
                return std::isspace(character) == 0;
            });

    if (owner.empty() ||
        owner.size() > 120 ||
        !contains_non_whitespace) {
        throw ValidationError(
            "owner must contain 1 to 120 characters and cannot be whitespace-only");
    }
}

void PaymentService::validate_idempotency_key(
    const std::string& key) {

    if (key.size() < 8 || key.size() > 128) {
        throw ValidationError(
            "idempotency_key must contain 8 to 128 characters");
    }
}

Account PaymentService::create_account(
    const std::string& owner,
    const std::string& currency) {

    validate_owner(owner);

    return database_.create_account(
        generate_id("acct"),
        owner,
        normalize_currency(currency),
        now_epoch_seconds());
}

Account PaymentService::get_account(
    const std::string& account_id) const {

    const auto account =
        database_.get_account(account_id);

    if (!account.has_value()) {
        throw NotFoundError("account not found");
    }

    return *account;
}

Account PaymentService::freeze_account(
    const std::string& account_id) {

    std::lock_guard lock(operation_mutex_);

    (void)get_account(account_id);

    database_.set_account_status(
        account_id,
        AccountStatus::Frozen,
        now_epoch_seconds());

    return get_account(account_id);
}

Transaction PaymentService::deposit(
    const std::string& account_id,
    std::int64_t amount_minor,
    const std::string& idempotency_key) {

    validate_idempotency_key(idempotency_key);

    if (amount_minor <= 0) {
        throw ValidationError(
            "amount_minor must be positive");
    }

    std::lock_guard lock(operation_mutex_);

    const Account account =
        get_account(account_id);

    if (account.status != AccountStatus::Active) {
        throw RiskRejectedError(
            "account_not_active");
    }

    const std::string request_fingerprint =
        fingerprint(
            "deposit",
            account_id,
            "",
            amount_minor);

    if (const auto existing =
            database_.find_transaction_by_idempotency_key(
                idempotency_key);
        existing.has_value()) {

        if (existing->request_fingerprint !=
            request_fingerprint) {
            throw ConflictError(
                "idempotency key was already used for a different request");
        }

        return *existing;
    }

    Transaction transaction;
    transaction.id = generate_id("txn");
    transaction.idempotency_key = idempotency_key;
    transaction.request_fingerprint = request_fingerprint;
    transaction.type = TransactionType::Deposit;
    transaction.destination_account_id = account_id;
    transaction.amount_minor = amount_minor;
    transaction.currency = account.currency;
    transaction.status = "completed";
    transaction.created_at_epoch_seconds =
        now_epoch_seconds();

    return database_.record_deposit(transaction);
}

Transaction PaymentService::transfer(
    const std::string& source_account_id,
    const std::string& destination_account_id,
    std::int64_t amount_minor,
    const std::string& idempotency_key) {

    validate_idempotency_key(idempotency_key);

    if (source_account_id == destination_account_id) {
        throw ValidationError(
            "source and destination accounts must differ");
    }

    if (amount_minor <= 0) {
        throw ValidationError(
            "amount_minor must be positive");
    }

    std::lock_guard lock(operation_mutex_);

    const std::string request_fingerprint =
        fingerprint(
            "transfer",
            source_account_id,
            destination_account_id,
            amount_minor);

    if (const auto existing =
            database_.find_transaction_by_idempotency_key(
                idempotency_key);
        existing.has_value()) {

        if (existing->request_fingerprint !=
            request_fingerprint) {
            throw ConflictError(
                "idempotency key was already used for a different request");
        }

        return *existing;
    }

    const Account source =
        get_account(source_account_id);

    const Account destination =
        get_account(destination_account_id);

    // AtlasPay currently has no FX conversion engine.
    // Transfers are therefore only valid between accounts
    // denominated in the same currency.
    if (source.currency != destination.currency) {
        throw ValidationError(
            "source and destination accounts must use the same currency");
    }

    if (source.balance_minor < amount_minor) {
        throw ValidationError(
            "insufficient funds");
    }

    const auto now =
        now_epoch_seconds();

    const RiskDecision decision =
        risk_engine_.evaluate_transfer(
            source,
            destination,
            amount_minor,
            now);

    if (!decision.approved) {
        throw RiskRejectedError(
            decision.reason);
    }

    Transaction transaction;
    transaction.id = generate_id("txn");
    transaction.idempotency_key = idempotency_key;
    transaction.request_fingerprint = request_fingerprint;
    transaction.type = TransactionType::Transfer;
    transaction.source_account_id =
        source_account_id;
    transaction.destination_account_id =
        destination_account_id;
    transaction.amount_minor =
        amount_minor;
    transaction.currency =
        source.currency;
    transaction.status =
        "completed";
    transaction.created_at_epoch_seconds =
        now;

    try {
        return database_.record_transfer(
            transaction);
    } catch (const std::runtime_error& error) {

        if (std::string(error.what()) ==
            "insufficient funds") {
            throw ValidationError(
                "insufficient funds");
        }

        throw;
    }
}

std::vector<Transaction>
PaymentService::list_transactions(
    const std::string& account_id,
    std::size_t limit) const {

    (void)get_account(account_id);

    return database_.list_transactions_for_account(
        account_id,
        limit);
}

}  // namespace atlaspay
