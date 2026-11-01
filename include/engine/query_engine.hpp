#pragma once

#include <vector>
#include <memory>
#include <chrono>
#include "columnar/columnar_table.hpp"
#include "columnar/columnar_pivot.hpp"
#include "simd/simd_black_scholes.hpp"
#include "engine/local_accumulator.hpp"
#include "engine/reduction_tree.hpp"

namespace riskdb {

struct QueryExecutionStats {
    double compute_time_ms{0.0};
    double reduction_time_ms{0.0};
    double total_engine_time_ms{0.0};
    double throughput_rows_per_sec{0.0};
    size_t rows_processed{0};
    size_t thread_count{1};
};

class QueryEngine {
public:
    explicit QueryEngine(size_t num_threads = 4, size_t num_portfolios = 500);

    void set_thread_count(size_t num_threads) noexcept { num_threads_ = num_threads; }
    size_t thread_count() const noexcept { return num_threads_; }

    void set_portfolio_count(size_t count) noexcept { num_portfolios_ = count; }
    size_t portfolio_count() const noexcept { return num_portfolios_; }

    /**
     * @brief Executes the end-to-end parallel vectorized risk calculation pipeline on a ColumnarTable.
     * Isolates compute and reduction execution from I/O as per benchmark specification.
     * 
     * @param table Input columnar table
     * @param stats Output execution telemetry
     * @return std::vector<PortfolioRiskMetrics> Per-portfolio aggregated risk metrics
     */
    std::vector<PortfolioRiskMetrics> execute_risk_query(
        const ColumnarTable& table,
        QueryExecutionStats& stats
    );

private:
    size_t num_threads_{4};
    size_t num_portfolios_{500};
    std::vector<ThreadLocalAccumulator> thread_accumulators_;
};

} // namespace riskdb
