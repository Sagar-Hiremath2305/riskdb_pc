#pragma once

#include <vector>
#include <string>
#include <cstddef>
#include "columnar/columnar_table.hpp"
#include "engine/query_engine.hpp"

namespace riskdb {

struct StrongScalingResult {
    size_t threads;
    size_t rows;
    double time_ms;
    double speedup;
    double efficiency;
    double throughput_m_rows_sec;
};

struct WeakScalingResult {
    size_t threads;
    size_t rows;
    double time_ms;
    double efficiency;
    double throughput_m_rows_sec;
};

struct ArchitecturalComparisonResult {
    size_t rows;
    double traditional_transfer_time_ms;
    double traditional_compute_time_ms;
    double traditional_total_time_ms;
    double traditional_throughput_m_rows_sec;

    double riskdb_compute_time_ms;
    double riskdb_reduction_time_ms;
    double riskdb_total_time_ms;
    double riskdb_throughput_m_rows_sec;

    double overall_speedup;
};

class BenchmarkRunner {
public:
    BenchmarkRunner() = default;

    /**
     * @brief Evaluates Strong Scaling (Amdahl's Law) on fixed dataset scaling threads from 1 to max_threads.
     */
    static std::vector<StrongScalingResult> run_strong_scaling(
        const ColumnarTable& table,
        size_t max_threads = 16,
        size_t num_portfolios = 500,
        int warmup_runs = 1,
        int benchmark_runs = 3
    );

    /**
     * @brief Evaluates Weak Scaling (Gustafson's Law) scaling data proportionately with thread count.
     */
    static std::vector<WeakScalingResult> run_weak_scaling(
        size_t rows_per_thread = 500'000,
        size_t max_threads = 16,
        size_t num_portfolios = 500
    );

    /**
     * @brief Compares Traditional Tiered Architecture against RiskDB Zero-Copy In-Engine Architecture.
     */
    static ArchitecturalComparisonResult run_architectural_comparison(
        const std::vector<RawOptionRecord>& raw_records,
        const ColumnarTable& columnar_table,
        size_t threads = 16,
        size_t num_portfolios = 500,
        double network_bandwidth_gbps = 10.0 // 10 Gbps typical VM inter-connect
    );

    /**
     * @brief Formats results as Markdown / ASCII tables for academic reporting.
     */
    static void print_report(
        const std::vector<StrongScalingResult>& strong,
        const std::vector<WeakScalingResult>& weak,
        const ArchitecturalComparisonResult& arch
    );
};

} // namespace riskdb
