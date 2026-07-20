#pragma once

#include "atlaspay/database.hpp"
#include "atlaspay/models.hpp"

#include <cstdint>
#include <string>

namespace atlaspay {

struct RiskLimits {
    std::int64_t max_single_transfer_minor{1'000'000};
    std::int64_t max_daily_outgoing_minor{2'500'000};
    std::int64_t velocity_window_seconds{60};
    std::int64_t max_transfers_per_velocity_window{5};
};

class RiskEngine {
public:
    explicit RiskEngine(const Database& database, RiskLimits limits = {});

    RiskDecision evaluate_transfer(const Account& source,
                                   const Account& destination,
                                   std::int64_t amount_minor,
                                   std::int64_t now_epoch_seconds) const;

private:
    const Database& database_;
    RiskLimits limits_;
};

}  // namespace atlaspay
