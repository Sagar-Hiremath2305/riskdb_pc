#include "columnar/columnar_table.hpp"

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
    ++num_records_;
}

size_t ColumnarTable::memory_footprint_bytes() const noexcept {
    return num_records_ * (
        sizeof(float) * 6 +         // spot, strike, expiry, vol, rate, quantity
        sizeof(uint8_t) +           // option_type
        sizeof(uint16_t) +          // portfolio_id
        sizeof(uint32_t) +          // underlying_id
        sizeof(uint64_t)            // contract_id
    );
}

} // namespace riskdb
