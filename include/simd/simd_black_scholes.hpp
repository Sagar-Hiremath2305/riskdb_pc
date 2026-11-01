#pragma once

#include <cstddef>
#include <cstdint>
#include "columnar/columnar_table.hpp"
#include "common/types.hpp"

namespace riskdb {

class SIMDBlackScholesKernel {
public:
    /**
     * @brief Vectorized Black-Scholes evaluation on a columnar slice [start_idx, start_idx + count).
     * Populates output buffers for price, delta, gamma, and vega.
     */
    static void evaluate_batch(
        const ColumnarTable& table,
        size_t start_idx,
        size_t count,
        float* out_prices,
        float* out_deltas,
        float* out_gammas,
        float* out_vegas
    ) noexcept;

    /**
     * @brief Fused Vectorized Execution + Accumulation Pipeline.
     * Evaluates Black-Scholes math in SIMD registers and directly accumulates into
     * a thread-local portfolio metrics buffer, bypassing memory writeback to conserve memory bandwidth.
     * 
     * @param table Input columnar table
     * @param start_idx Starting index for the chunk
     * @param count Number of option contracts in this chunk
     * @param local_portfolio_deltas Array/buffer of per-portfolio Delta sums
     * @param local_portfolio_values Array/buffer of per-portfolio Value sums
     * @param local_portfolio_counts Array/buffer of per-portfolio contract counts
     * @param num_portfolios Number of active portfolios
     */
    static void evaluate_and_accumulate_chunk(
        const ColumnarTable& table,
        size_t start_idx,
        size_t count,
        double* local_portfolio_deltas,
        double* local_portfolio_values,
        double* local_portfolio_gammas,
        double* local_portfolio_vegas,
        uint64_t* local_portfolio_counts,
        size_t num_portfolios
    ) noexcept;
};

} // namespace riskdb
