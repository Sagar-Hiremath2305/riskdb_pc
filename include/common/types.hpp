#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "alignment.hpp"

namespace riskdb {

enum class OptionType : uint8_t {
    CALL = 0,
    PUT = 1
};

/**
 * @brief Raw tuple format representing an individual option contract record from PostgreSQL.
 */
struct RawOptionRecord {
    uint64_t contract_id;
    uint32_t underlying_id;
    uint16_t portfolio_id;
    uint8_t option_type;      // 0 = CALL, 1 = PUT
    float spot_price;         // S > 0
    float strike_price;       // K > 0
    float time_to_expiry;     // tau > 0 (in years)
    float volatility;         // sigma > 0
    float risk_free_rate;     // r >= 0
    float quantity;           // Contract quantity (long > 0, short < 0)

    bool is_valid() const noexcept {
        return (spot_price > 0.0f) &&
               (strike_price > 0.0f) &&
               (time_to_expiry > 0.0f) &&
               (volatility > 0.0f) &&
               (risk_free_rate >= 0.0f) &&
               (option_type <= 1);
    }
};

/**
 * @brief Aggregated portfolio risk metrics computed via parallel reduction.
 */
struct alignas(CACHE_LINE_SIZE) PortfolioRiskMetrics {
    uint32_t portfolio_id{0};
    double total_delta{0.0};
    double total_gamma{0.0};
    double total_vega{0.0};
    double total_theta{0.0};
    double total_value{0.0};
    uint64_t contract_count{0};

    PortfolioRiskMetrics() = default;
    explicit PortfolioRiskMetrics(uint32_t pid) : portfolio_id(pid) {}

    PortfolioRiskMetrics& operator+=(const PortfolioRiskMetrics& rhs) noexcept {
        total_delta += rhs.total_delta;
        total_gamma += rhs.total_gamma;
        total_vega  += rhs.total_vega;
        total_theta += rhs.total_theta;
        total_value += rhs.total_value;
        contract_count += rhs.contract_count;
        return *this;
    }
};

using AlignedFloatVector = std::vector<float, AlignedAllocator<float>>;
using AlignedUInt8Vector = std::vector<uint8_t, AlignedAllocator<uint8_t>>;
using AlignedUInt16Vector = std::vector<uint16_t, AlignedAllocator<uint16_t>>;
using AlignedUInt32Vector = std::vector<uint32_t, AlignedAllocator<uint32_t>>;
using AlignedUInt64Vector = std::vector<uint64_t, AlignedAllocator<uint64_t>>;

} // namespace riskdb
