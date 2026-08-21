#include "atlaspay/database.hpp"
#include "atlaspay/payment_service.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>

namespace {

double percentile_microseconds(
    std::vector<double> latencies,
    double percentile) {

    if (latencies.empty()) {
        return 0.0;
    }

    std::sort(latencies.begin(), latencies.end());

    const double position =
        percentile * static_cast<double>(latencies.size() - 1);

    const std::size_t index =
        static_cast<std::size_t>(position);

    return latencies[index];
}

}  // namespace

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

        atlaspay::RiskLimits limits;
        limits.max_single_transfer_minor = 10'000'000;
        limits.max_daily_outgoing_minor = 1'000'000'000;
        limits.max_transfers_per_velocity_window = 1'000'000;

        atlaspay::PaymentService service(database, limits);

        const auto source =
            service.create_account(
                "Benchmark Source",
                "USD");

        const auto destination =
            service.create_account(
                "Benchmark Destination",
                "USD");

        service.deposit(
            source.id,
            static_cast<std::int64_t>(kTransferCount) *
                    kTransferAmount +
                100'000,
            "benchmark-initial-deposit");

        std::vector<double> latencies_microseconds;
        latencies_microseconds.reserve(kTransferCount);

        std::cout
            << "AtlasPay Transaction Benchmark\n"
            << "==============================\n"
            << "Transfers: "
            << kTransferCount
            << '\n'
            << "Amount per transfer: "
            << kTransferAmount
            << " minor units\n\n";

        const auto benchmark_start =
            std::chrono::steady_clock::now();

        for (std::size_t index = 0;
             index < kTransferCount;
             ++index) {

            const auto transfer_start =
                std::chrono::steady_clock::now();

            service.transfer(
                source.id,
                destination.id,
                kTransferAmount,
                "benchmark-transfer-" +
                    std::to_string(index));

            const auto transfer_end =
                std::chrono::steady_clock::now();

            const double latency_us =
                std::chrono::duration<double, std::micro>(
                    transfer_end - transfer_start)
                    .count();

            latencies_microseconds.push_back(latency_us);
        }

        const auto benchmark_end =
            std::chrono::steady_clock::now();

        const double elapsed_seconds =
            std::chrono::duration<double>(
                benchmark_end - benchmark_start)
                .count();

        const double transfers_per_second =
            static_cast<double>(kTransferCount) /
            elapsed_seconds;

        const double average_latency_us =
            (elapsed_seconds * 1'000'000.0) /
            static_cast<double>(kTransferCount);

        const double median_latency_us =
            percentile_microseconds(
                latencies_microseconds,
                0.50);

        const double p95_latency_us =
            percentile_microseconds(
                latencies_microseconds,
                0.95);

        const double p99_latency_us =
            percentile_microseconds(
                latencies_microseconds,
                0.99);

        const auto final_source =
            service.get_account(source.id);

        const auto final_destination =
            service.get_account(destination.id);

        const std::int64_t expected_destination =
            static_cast<std::int64_t>(
                kTransferCount) *
            kTransferAmount;

        std::cout
            << std::fixed
            << std::setprecision(2);

        std::cout
            << "Elapsed time: "
            << elapsed_seconds
            << " seconds\n";

        std::cout
            << "Throughput: "
            << transfers_per_second
            << " transfers/second\n";

        std::cout
            << "Average latency: "
            << average_latency_us
            << " us\n";

        std::cout
            << "Median latency: "
            << median_latency_us
            << " us\n";

        std::cout
            << "p95 latency: "
            << p95_latency_us
            << " us\n";

        std::cout
            << "p99 latency: "
            << p99_latency_us
            << " us\n";

        std::cout
            << "\nFinal balances\n";

        std::cout
            << "Source: "
            << final_source.balance_minor
            << '\n';

        std::cout
            << "Destination: "
            << final_destination.balance_minor
            << '\n';

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
