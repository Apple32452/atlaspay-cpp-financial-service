#include "atlaspay/database.hpp"
#include "atlaspay/payment_service.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <unistd.h>

int main() {
    constexpr std::size_t kTransferCount = 10'000;
    constexpr std::int64_t kTransferAmount = 100;

    const std::string database_path =
        "/tmp/atlaspay_benchmark_" +
        std::to_string(::getpid()) +
        ".db";

    std::filesystem::remove(database_path);
    std::filesystem::remove(database_path + "-wal");
    std::filesystem::remove(database_path + "-shm");

    try {
        atlaspay::Database database(database_path);
        database.initialize_schema();

        // Increase limits so the benchmark measures transaction
        // processing rather than intentionally triggering risk controls.
        atlaspay::RiskLimits limits;
        limits.max_single_transfer_minor = 10'000'000;
        limits.max_daily_outgoing_minor = 1'000'000'000;
        limits.max_transfers_per_velocity_window = 1'000'000;

        atlaspay::PaymentService service(database, limits);

        const auto source =
            service.create_account("Benchmark Source", "USD");

        const auto destination =
            service.create_account("Benchmark Destination", "USD");

        service.deposit(
            source.id,
            static_cast<std::int64_t>(kTransferCount) *
                kTransferAmount +
                100'000,
            "benchmark-initial-deposit");

        std::cout << "AtlasPay Transaction Benchmark\n";
        std::cout << "==============================\n";
        std::cout << "Transfers: "
                  << kTransferCount
                  << '\n';

        std::cout << "Amount per transfer: "
                  << kTransferAmount
                  << " minor units\n\n";

        const auto start =
            std::chrono::steady_clock::now();

        for (std::size_t index = 0;
             index < kTransferCount;
             ++index) {

            service.transfer(
                source.id,
                destination.id,
                kTransferAmount,
                "benchmark-transfer-" +
                    std::to_string(index));
        }

        const auto end =
            std::chrono::steady_clock::now();

        const auto elapsed =
            std::chrono::duration<double>(
                end - start)
                .count();

        const double transfers_per_second =
            static_cast<double>(kTransferCount) /
            elapsed;

        const double average_latency_us =
            (elapsed * 1'000'000.0) /
            static_cast<double>(kTransferCount);

        const auto final_source =
            service.get_account(source.id);

        const auto final_destination =
            service.get_account(destination.id);

        std::cout << std::fixed
                  << std::setprecision(2);

        std::cout << "Elapsed time: "
                  << elapsed
                  << " seconds\n";

        std::cout << "Throughput: "
                  << transfers_per_second
                  << " transfers/second\n";

        std::cout << "Average latency: "
                  << average_latency_us
                  << " microseconds/transfer\n";

        std::cout << "\nFinal balances\n";
        std::cout << "Source: "
                  << final_source.balance_minor
                  << '\n';

        std::cout << "Destination: "
                  << final_destination.balance_minor
                  << '\n';

        const std::int64_t expected_destination =
            static_cast<std::int64_t>(
                kTransferCount) *
            kTransferAmount;

        if (final_destination.balance_minor !=
            expected_destination) {

            std::cerr
                << "Benchmark validation failed: "
                << "destination balance mismatch\n";

            return 1;
        }

        std::cout
            << "\nBenchmark validation passed.\n";

        std::filesystem::remove(database_path);
        std::filesystem::remove(database_path + "-wal");
        std::filesystem::remove(database_path + "-shm");

        return 0;

    } catch (const std::exception& error) {
        std::cerr
            << "Benchmark failure: "
            << error.what()
            << '\n';

        std::filesystem::remove(database_path);
        std::filesystem::remove(database_path + "-wal");
        std::filesystem::remove(database_path + "-shm");

        return 1;
    }
}
