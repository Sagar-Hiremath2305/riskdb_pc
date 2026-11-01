#include "engine/query_engine.hpp"
#include <chrono>
#include <iostream>

#if defined(_OPENMP)
#include <omp.h>
#endif

namespace riskdb {

using Clock = std::chrono::high_resolution_clock;

QueryEngine::QueryEngine(size_t num_threads, size_t num_portfolios)
    : num_threads_(num_threads), num_portfolios_(num_portfolios) {
    if (num_threads_ == 0) num_threads_ = 1;
    thread_accumulators_.resize(num_threads_);
    for (auto& acc : thread_accumulators_) {
        acc.init(num_portfolios_);
    }
}

std::vector<PortfolioRiskMetrics> QueryEngine::execute_risk_query(
    const ColumnarTable& table,
    QueryExecutionStats& stats
) {
    const size_t total_rows = table.size();
    stats.rows_processed = total_rows;
    stats.thread_count = num_threads_;

    if (total_rows == 0) {
        stats.compute_time_ms = 0.0;
        stats.reduction_time_ms = 0.0;
        stats.total_engine_time_ms = 0.0;
        stats.throughput_rows_per_sec = 0.0;
        return {};
    }

    // Ensure thread accumulators match thread count and portfolio count
    if (thread_accumulators_.size() != num_threads_) {
        thread_accumulators_.resize(num_threads_);
    }
    for (size_t t = 0; t < num_threads_; ++t) {
        if (thread_accumulators_[t].num_portfolios() != num_portfolios_) {
            thread_accumulators_[t].init(num_portfolios_);
        } else {
            thread_accumulators_[t].reset();
        }
    }

    // Partition columnar table into cache-aligned chunks
    const auto chunks = ColumnarPivot::create_chunks(table, num_threads_, 16);
    const size_t actual_chunks = chunks.size();

    const auto t_start = Clock::now();

    // Stage 1: Parallel SIMD Execution & Thread-Local Lock-Free Accumulation
    #if defined(_OPENMP)
    #pragma omp parallel num_threads(num_threads_)
    {
        const int tid = omp_get_thread_num();
        if (tid < static_cast<int>(actual_chunks)) {
            const auto& chunk = chunks[tid];
            auto& acc = thread_accumulators_[tid];

            SIMDBlackScholesKernel::evaluate_and_accumulate_chunk(
                *chunk.table,
                chunk.start_index,
                chunk.length,
                acc.deltas_data(),
                acc.values_data(),
                acc.gammas_data(),
                acc.vegas_data(),
                acc.counts_data(),
                num_portfolios_
            );
        }
    }
    #else
    for (size_t tid = 0; tid < actual_chunks; ++tid) {
        const auto& chunk = chunks[tid];
        auto& acc = thread_accumulators_[tid];
        SIMDBlackScholesKernel::evaluate_and_accumulate_chunk(
            *chunk.table,
            chunk.start_index,
            chunk.length,
            acc.deltas_data(),
            acc.values_data(),
            acc.gammas_data(),
            acc.vegas_data(),
            acc.counts_data(),
            num_portfolios_
        );
    }
    #endif

    const auto t_compute = Clock::now();

    // Stage 2: Logarithmic O(log T) Parallel Reduction Tree
    ReductionTree::reduce(thread_accumulators_, actual_chunks);

    const auto t_reduction = Clock::now();

    // Calculate timings in milliseconds
    const double compute_ms = std::chrono::duration<double, std::milli>(t_compute - t_start).count();
    const double reduction_ms = std::chrono::duration<double, std::milli>(t_reduction - t_compute).count();
    const double total_ms = compute_ms + reduction_ms;

    stats.compute_time_ms = compute_ms;
    stats.reduction_time_ms = reduction_ms;
    stats.total_engine_time_ms = total_ms;
    stats.throughput_rows_per_sec = (total_ms > 0.0) ? (static_cast<double>(total_rows) / (total_ms / 1000.0)) : 0.0;

    return thread_accumulators_[0].export_metrics();
}

} // namespace riskdb
