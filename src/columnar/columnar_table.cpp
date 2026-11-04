#include "columnar/columnar_table.hpp"
#include "common/math_utils.hpp"
#include <algorithm>

namespace riskdb {

ColumnarTable::ColumnarTable(size_t capacity) {
    reserve(capacity);
}

void ColumnarTable::reserve(size_t capacity) {
    capacity_ = capacity;
    spot_prices_.reserve(capacity);
    strike_prices_.reserve(capacity);
    times_to_expiry_.reserve(capacity);
    volatilities_.reserve(capacity);
    risk_free_rates_.reserve(capacity);
    quantities_.reserve(capacity);
    option_types_.reserve(capacity);
    portfolio_ids_.reserve(capacity);
    underlying_ids_.reserve(capacity);
    contract_ids_.reserve(capacity);

    computed_prices_.reserve(capacity);
    computed_deltas_.reserve(capacity);
    computed_gammas_.reserve(capacity);
    computed_vegas_.reserve(capacity);
    computed_thetas_.reserve(capacity);
}

void ColumnarTable::resize(size_t size) {
    num_records_ = size;
    capacity_ = std::max(capacity_, size);
    spot_prices_.resize(size);
    strike_prices_.resize(size);
    times_to_expiry_.resize(size);
    volatilities_.resize(size);
    risk_free_rates_.resize(size);
    quantities_.resize(size);
    option_types_.resize(size);
    portfolio_ids_.resize(size);
    underlying_ids_.resize(size);
    contract_ids_.resize(size);

    computed_prices_.resize(size, 0.0f);
    computed_deltas_.resize(size, 0.0f);
    computed_gammas_.resize(size, 0.0f);
    computed_vegas_.resize(size, 0.0f);
    computed_thetas_.resize(size, 0.0f);
}

void ColumnarTable::clear() noexcept {
    num_records_ = 0;
    spot_prices_.clear();
    strike_prices_.clear();
    times_to_expiry_.clear();
    volatilities_.clear();
    risk_free_rates_.clear();
    quantities_.clear();
    option_types_.clear();
    portfolio_ids_.clear();
    underlying_ids_.clear();
    contract_ids_.clear();

    computed_prices_.clear();
    computed_deltas_.clear();
    computed_gammas_.clear();
    computed_vegas_.clear();
    computed_thetas_.clear();
}

void ColumnarTable::append(const RawOptionRecord& rec) {
    spot_prices_.push_back(rec.spot_price);
    strike_prices_.push_back(rec.strike_price);
    times_to_expiry_.push_back(rec.time_to_expiry);
    volatilities_.push_back(rec.volatility);
    risk_free_rates_.push_back(rec.risk_free_rate);
    quantities_.push_back(rec.quantity);
    option_types_.push_back(rec.option_type);
    portfolio_ids_.push_back(rec.portfolio_id);
    underlying_ids_.push_back(rec.underlying_id);
    contract_ids_.push_back(rec.contract_id);

    computed_prices_.push_back(0.0f);
    computed_deltas_.push_back(0.0f);
    computed_gammas_.push_back(0.0f);
    computed_vegas_.push_back(0.0f);
    computed_thetas_.push_back(0.0f);

    ++num_records_;
}

size_t ColumnarTable::update_underlying_spot(uint32_t target_underlying_id, float new_spot_price) noexcept {
    size_t count = 0;
    #pragma omp parallel for reduction(+:count) schedule(static)
    for (size_t i = 0; i < num_records_; ++i) {
        if (underlying_ids_[i] == target_underlying_id) {
            spot_prices_[i] = new_spot_price;
            ++count;
        }
    }

    // Automatically update stored prices & Greeks in database memory for affected underlying
    recompute_stored_metrics(static_cast<int>(target_underlying_id));
    return count;
}

void ColumnarTable::update_all_spots(float multiplier) noexcept {
    #pragma omp parallel for schedule(static)
    for (size_t i = 0; i < num_records_; ++i) {
        spot_prices_[i] *= multiplier;
    }
    recompute_stored_metrics(-1);
}

void ColumnarTable::recompute_stored_metrics(int target_underlying) noexcept {
    if (computed_prices_.size() != num_records_) {
        computed_prices_.resize(num_records_, 0.0f);
        computed_deltas_.resize(num_records_, 0.0f);
        computed_gammas_.resize(num_records_, 0.0f);
        computed_vegas_.resize(num_records_, 0.0f);
        computed_thetas_.resize(num_records_, 0.0f);
    }

    #pragma omp parallel for schedule(static)
    for (size_t i = 0; i < num_records_; ++i) {
        if (target_underlying >= 0 && underlying_ids_[i] != static_cast<uint32_t>(target_underlying)) {
            continue;
        }

        OptionGreeks g = black_scholes_scalar(
            static_cast<double>(spot_prices_[i]),
            static_cast<double>(strike_prices_[i]),
            static_cast<double>(times_to_expiry_[i]),
            static_cast<double>(risk_free_rates_[i]),
            static_cast<double>(volatilities_[i]),
            option_types_[i] == 1
        );

        computed_prices_[i] = static_cast<float>(g.price);
        computed_deltas_[i] = static_cast<float>(g.delta);
        computed_gammas_[i] = static_cast<float>(g.gamma);
        computed_vegas_[i]  = static_cast<float>(g.vega);
        computed_thetas_[i] = static_cast<float>(g.theta);
    }
}

size_t ColumnarTable::memory_footprint_bytes() const noexcept {
    return num_records_ * (
        sizeof(float) * 6 +         // spot, strike, expiry, vol, rate, quantity
        sizeof(uint8_t) +           // option_type
        sizeof(uint16_t) +          // portfolio_id
        sizeof(uint32_t) +          // underlying_id
        sizeof(uint64_t) +          // contract_id
        sizeof(float) * 5           // computed_price, delta, gamma, vega, theta
    );
}

} // namespace riskdb
