#include "atlaspay/risk_engine.hpp"

namespace atlaspay {

RiskEngine::RiskEngine(const Database& database, RiskLimits limits)
    : database_(database), limits_(limits) {}

RiskDecision RiskEngine::evaluate_transfer(const Account& source,
                                           const Account& destination,
                                           std::int64_t amount_minor,
                                           std::int64_t now_epoch_seconds) const {
    if (source.status != AccountStatus::Active) {
        return {false, "source_account_not_active"};
    }
    if (destination.status != AccountStatus::Active) {
        return {false, "destination_account_not_active"};
    }
    if (source.currency != destination.currency) {
        return {false, "currency_mismatch"};
    }
    if (amount_minor > limits_.max_single_transfer_minor) {
        return {false, "single_transfer_limit_exceeded"};
    }

    const auto daily_outgoing = database_.outgoing_sum_since(source.id, now_epoch_seconds - 86'400);
    if (daily_outgoing + amount_minor > limits_.max_daily_outgoing_minor) {
        return {false, "daily_outgoing_limit_exceeded"};
    }

    const auto recent_count =
        database_.transfer_count_since(source.id, now_epoch_seconds - limits_.velocity_window_seconds);
    if (recent_count >= limits_.max_transfers_per_velocity_window) {
        return {false, "velocity_limit_exceeded"};
    }

    return {true, ""};
}

}  // namespace atlaspay
