#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <memory>
#include <string>
#include "common/types.hpp"
#include "common/alignment.hpp"

namespace riskdb {

/**
 * @brief Cache-aligned Columnar Table storing options contract attributes in contiguous arrays.
 * 
 * Enables zero-gather SIMD streaming vector loads directly into AVX2 / NEON registers.
 */
class ColumnarTable {
public:
    ColumnarTable() = default;
    explicit ColumnarTable(size_t capacity);

    void reserve(size_t capacity);
    void resize(size_t size);
    void clear() noexcept;

    // Append single contract (used during ingestion / pivoting)
    void append(const RawOptionRecord& record);

    size_t size() const noexcept { return num_records_; }
    size_t capacity() const noexcept { return capacity_; }

    // Direct pointers to 64-byte aligned contiguous arrays for SIMD kernels
    const float* spot_prices() const noexcept { return spot_prices_.data(); }
    const float* strike_prices() const noexcept { return strike_prices_.data(); }
    const float* times_to_expiry() const noexcept { return times_to_expiry_.data(); }
    const float* volatilities() const noexcept { return volatilities_.data(); }
    const float* risk_free_rates() const noexcept { return risk_free_rates_.data(); }
    const float* quantities() const noexcept { return quantities_.data(); }
    const uint8_t* option_types() const noexcept { return option_types_.data(); }
    const uint16_t* portfolio_ids() const noexcept { return portfolio_ids_.data(); }
    const uint32_t* underlying_ids() const noexcept { return underlying_ids_.data(); }
    const uint64_t* contract_ids() const noexcept { return contract_ids_.data(); }

    // Mutable pointers if required for in-place modifications
    float* spot_prices_mut() noexcept { return spot_prices_.data(); }
    float* strike_prices_mut() noexcept { return strike_prices_.data(); }
    float* times_to_expiry_mut() noexcept { return times_to_expiry_.data(); }
    float* volatilities_mut() noexcept { return volatilities_.data(); }
    float* risk_free_rates_mut() noexcept { return risk_free_rates_.data(); }
    float* quantities_mut() noexcept { return quantities_.data(); }
    uint8_t* option_types_mut() noexcept { return option_types_.data(); }
    uint16_t* portfolio_ids_mut() noexcept { return portfolio_ids_.data(); }
    uint32_t* underlying_ids_mut() noexcept { return underlying_ids_.data(); }
    uint64_t* contract_ids_mut() noexcept { return contract_ids_.data(); }

    // Memory footprint calculation in bytes
    size_t memory_footprint_bytes() const noexcept;

private:
    size_t num_records_{0};
    size_t capacity_{0};

    AlignedFloatVector  spot_prices_;
    AlignedFloatVector  strike_prices_;
    AlignedFloatVector  times_to_expiry_;
    AlignedFloatVector  volatilities_;
    AlignedFloatVector  risk_free_rates_;
    AlignedFloatVector  quantities_;
    AlignedUInt8Vector  option_types_;
    AlignedUInt16Vector portfolio_ids_;
    AlignedUInt32Vector underlying_ids_;
    AlignedUInt64Vector contract_ids_;
};

/**
 * @brief Represents a cache-aligned slice/partition of the ColumnarTable assigned to a worker thread.
 */
struct ColumnarChunk {
    size_t start_index{0};
    size_t length{0};
    const ColumnarTable* table{nullptr};

    ColumnarChunk() = default;
    ColumnarChunk(size_t start, size_t len, const ColumnarTable* tbl)
        : start_index(start), length(len), table(tbl) {}
};

} // namespace riskdb
