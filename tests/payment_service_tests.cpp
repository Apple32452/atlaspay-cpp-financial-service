#include "atlaspay/database.hpp"
#include "atlaspay/payment_service.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

namespace {

// Simple assertion helper used by the test program.
void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Verify that a function throws the expected exception type.
template <typename Exception, typename Function>
void expect_throws(Function&& function, const std::string& message) {
    try {
        function();
    } catch (const Exception&) {
        return;
    }

    throw std::runtime_error(message);
}

}  // namespace

int main() {
    // Each test run uses its own temporary SQLite database.
    const std::string database_path =
        "/tmp/atlaspay_test_" + std::to_string(::getpid()) + ".db";

    // Clean up any database left over from a previous interrupted run.
    std::filesystem::remove(database_path);
    std::filesystem::remove(database_path + "-wal");
    std::filesystem::remove(database_path + "-shm");

    try {
        // ------------------------------------------------------------
        // Database and service setup
        // ------------------------------------------------------------

        atlaspay::Database database(database_path);
        database.initialize_schema();

        atlaspay::RiskLimits limits;
        limits.max_single_transfer_minor = 100'000;
        limits.max_daily_outgoing_minor = 150'000;
        limits.max_transfers_per_velocity_window = 10;

        atlaspay::PaymentService service(database, limits);

        // ------------------------------------------------------------
        // Account-owner validation
        // ------------------------------------------------------------

        expect_throws<atlaspay::ValidationError>(
            [&] {
                service.create_account("", "USD");
            },
            "empty account owner should be rejected");

        expect_throws<atlaspay::ValidationError>(
            [&] {
                service.create_account("   ", "USD");
            },
            "whitespace-only account owner should be rejected");

        // ------------------------------------------------------------
        // Currency validation
        // ------------------------------------------------------------

        expect_throws<atlaspay::ValidationError>(
            [&] {
                service.create_account(
                    "Invalid Currency User",
                    "US");
            },
            "two-letter currency should be rejected");

        expect_throws<atlaspay::ValidationError>(
            [&] {
                service.create_account(
                    "Invalid Currency User",
                    "US1");
            },
            "currency containing digits should be rejected");

        expect_throws<atlaspay::ValidationError>(
            [&] {
                service.create_account(
                    "Invalid Currency User",
                    "$$$$");
            },
            "invalid currency length should be rejected");

        expect_throws<atlaspay::ValidationError>(
            [&] {
                service.create_account(
                    "Invalid Currency User",
                    "U$D");
            },
            "currency containing symbols should be rejected");

        // ------------------------------------------------------------
        // Account creation and currency normalization
        // ------------------------------------------------------------

        const auto alice =
            service.create_account("Alice", "usd");

        const auto bob =
            service.create_account("Bob", "USD");

        expect(
            alice.currency == "USD",
            "currency normalization failed");

        expect(
            alice.balance_minor == 0,
            "new account must start at zero");

        expect(
            bob.balance_minor == 0,
            "new destination account must start at zero");


// ------------------------------------------------------------
// Missing-account validation
// ------------------------------------------------------------

expect_throws<atlaspay::NotFoundError>(
    [&] {
        service.get_account("acct_does_not_exist");
    },
    "getting a nonexistent account should fail");

expect_throws<atlaspay::NotFoundError>(
    [&] {
        service.deposit(
            "acct_does_not_exist",
            1'000,
            "missing-deposit-0001");
    },
    "deposit to nonexistent account should fail");

expect_throws<atlaspay::NotFoundError>(
    [&] {
        service.transfer(
            "acct_does_not_exist",
            bob.id,
            1'000,
            "missing-source-0001");
    },
    "transfer from nonexistent source account should fail");

expect_throws<atlaspay::NotFoundError>(
    [&] {
        service.transfer(
            alice.id,
            "acct_does_not_exist",
            1'000,
            "missing-destination-0001");
    },
    "transfer to nonexistent destination account should fail");
        // ------------------------------------------------------------
        // Deposits
        // ------------------------------------------------------------

        const auto deposit =
            service.deposit(
                alice.id,
                100'000,
                "deposit-alice-0001");

        expect(
            deposit.amount_minor == 100'000,
            "deposit amount mismatch");

        expect(
            service.get_account(alice.id).balance_minor == 100'000,
            "deposit balance mismatch");

        // ------------------------------------------------------------
        // Deposit idempotency
        // ------------------------------------------------------------

        const auto replay =
            service.deposit(
                alice.id,
                100'000,
                "deposit-alice-0001");

        expect(
            replay.id == deposit.id,
            "idempotent replay returned a different transaction");

        expect(
            service.get_account(alice.id).balance_minor == 100'000,
            "idempotent replay duplicated funds");

        // Same idempotency key with a different request must fail.
        expect_throws<atlaspay::ConflictError>(
            [&] {
                service.deposit(
                    alice.id,
                    99'999,
                    "deposit-alice-0001");
            },
            "reusing an idempotency key for a changed request should fail");

        // ------------------------------------------------------------
        // Self-transfer validation
        // ------------------------------------------------------------

        expect_throws<atlaspay::ValidationError>(
            [&] {
                service.transfer(
                    alice.id,
                    alice.id,
                    1'000,
                    "self-transfer-0001");
            },
            "self-transfer should be rejected");

        // ------------------------------------------------------------
        // Normal transfer
        // ------------------------------------------------------------

        const auto transfer =
            service.transfer(
                alice.id,
                bob.id,
                25'000,
                "transfer-alice-bob-0001");

        expect(
            transfer.status == "completed",
            "transfer did not complete");

        expect(
            service.get_account(alice.id).balance_minor == 75'000,
            "source balance mismatch");

        expect(
            service.get_account(bob.id).balance_minor == 25'000,
            "destination balance mismatch");

        // ------------------------------------------------------------
        // Transfer idempotency
        // ------------------------------------------------------------

        const auto transfer_replay =
            service.transfer(
                alice.id,
                bob.id,
                25'000,
                "transfer-alice-bob-0001");

        expect(
            transfer_replay.id == transfer.id,
            "transfer replay returned a different transaction");

        expect(
            service.get_account(alice.id).balance_minor == 75'000,
            "transfer replay duplicated debit");

        expect(
            service.get_account(bob.id).balance_minor == 25'000,
            "transfer replay duplicated credit");

        // ------------------------------------------------------------
        // Insufficient funds
        // ------------------------------------------------------------

        expect_throws<atlaspay::ValidationError>(
            [&] {
                service.transfer(
                    alice.id,
                    bob.id,
                    80'000,
                    "transfer-insufficient-0001");
            },
            "insufficient funds should fail");

        // ------------------------------------------------------------
        // Risk limit
        // ------------------------------------------------------------

        service.deposit(
            alice.id,
            100'000,
            "deposit-alice-0002");

        expect_throws<atlaspay::RiskRejectedError>(
            [&] {
                service.transfer(
                    alice.id,
                    bob.id,
                    100'001,
                    "transfer-too-large-0001");
            },
            "single transfer limit should reject the transfer");

        // ------------------------------------------------------------
        // Frozen-account protection
        // ------------------------------------------------------------

        service.freeze_account(bob.id);

        expect_throws<atlaspay::RiskRejectedError>(
            [&] {
                service.transfer(
                    alice.id,
                    bob.id,
                    1'000,
                    "transfer-frozen-0001");
            },
            "frozen destination should reject the transfer");


        const auto frozen_deposit_account =
    service.create_account("Frozen Deposit User", "USD");

service.freeze_account(frozen_deposit_account.id);

expect_throws<atlaspay::RiskRejectedError>(
    [&] {
        service.deposit(
            frozen_deposit_account.id,
            5'000,
            "deposit-frozen-0001");
    },
    "deposit to frozen account should be rejected");
        // ------------------------------------------------------------
        // Concurrent idempotency
        // ------------------------------------------------------------

        const auto carol =
            service.create_account("Carol", "USD");

        const auto dave =
            service.create_account("Dave", "USD");

        service.deposit(
            carol.id,
            50'000,
            "deposit-carol-0001");

        std::array<std::string, 8> concurrent_transaction_ids;
        std::vector<std::thread> workers;

        for (
            std::size_t index = 0;
            index < concurrent_transaction_ids.size();
            ++index) {

            workers.emplace_back(
                [&, index] {
                    concurrent_transaction_ids[index] =
                        service.transfer(
                            carol.id,
                            dave.id,
                            5'000,
                            "concurrent-transfer-0001")
                            .id;
                });
        }

        for (auto& worker : workers) {
            worker.join();
        }

        for (const auto& id : concurrent_transaction_ids) {
            expect(
                id == concurrent_transaction_ids.front(),
                "concurrent idempotent requests returned different transactions");
        }

        expect(
            service.get_account(carol.id).balance_minor == 45'000,
            "concurrent idempotency duplicated source debit");

        expect(
            service.get_account(dave.id).balance_minor == 5'000,
            "concurrent idempotency duplicated destination credit");

        // ------------------------------------------------------------
        // Transaction history
        // ------------------------------------------------------------

        const auto history =
            service.list_transactions(alice.id);

        expect(
            history.size() == 3,
            "unexpected transaction history size");

	expect_throws<atlaspay::ValidationError>(
    [&] {
        service.list_transactions(alice.id, 0);
    },
    "zero transaction history limit should be rejected");

expect_throws<atlaspay::ValidationError>(
    [&] {
        service.list_transactions(alice.id, 1001);
    },
    "transaction history limit above 1000 should be rejected");

const auto limited_history =
    service.list_transactions(alice.id, 2);

expect(
    limited_history.size() <= 2,
    "transaction history exceeded requested limit");
        // ------------------------------------------------------------
        // Success
        // ------------------------------------------------------------

        std::cout << "All AtlasPay tests passed.\n";

        std::filesystem::remove(database_path);
        std::filesystem::remove(database_path + "-wal");
        std::filesystem::remove(database_path + "-shm");

        return 0;

    } catch (const std::exception& error) {
        std::cerr
            << "Test failure: "
            << error.what()
            << '\n';

        std::filesystem::remove(database_path);
        std::filesystem::remove(database_path + "-wal");
        std::filesystem::remove(database_path + "-shm");

        return 1;
    }
}
