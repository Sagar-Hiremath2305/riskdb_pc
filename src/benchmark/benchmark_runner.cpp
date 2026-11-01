#include "benchmark/benchmark_runner.hpp"
#include "storage/data_generator.hpp"
#include "common/math_utils.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <algorithm>

namespace riskdb {

using Clock = std::chrono::high_resolution_clock;

std::vector<StrongScalingResult> BenchmarkRunner::run_strong_scaling(
    const ColumnarTable& table,
    size_t max_threads,
    size_t num_portfolios,
    int warmup_runs,
    int benchmark_runs
) {
    std::vector<StrongScalingResult> results;
    const std::vector<size_t> thread_counts = {1, 2, 4, 8, 16};

    QueryExecutionStats stats;
    double t1_time_ms = 0.0;

    for (size_t threads : thread_counts) {
        if (threads > max_threads) break;

        QueryEngine engine(threads, num_portfolios);

        // Warmup runs
        for (int w = 0; w < warmup_runs; ++w) {
            engine.execute_risk_query(table, stats);
        }

        // Benchmark runs
        double total_ms = 0.0;
        for (int b = 0; b < benchmark_runs; ++b) {
            engine.execute_risk_query(table, stats);
            total_ms += stats.total_engine_time_ms;
        }

        const double avg_time_ms = total_ms / benchmark_runs;
        if (threads == 1) {
            t1_time_ms = avg_time_ms;
        }

        const double speedup = (avg_time_ms > 0.0) ? (t1_time_ms / avg_time_ms) : 1.0;
        const double efficiency = speedup / static_cast<double>(threads);
        const double throughput = (avg_time_ms > 0.0)
            ? (static_cast<double>(table.size()) / (avg_time_ms / 1000.0) / 1'000'000.0)
            : 0.0;

        results.push_back({
            threads,
            table.size(),
            avg_time_ms,
            speedup,
            efficiency,
            throughput
        });
    }

    return results;
}

std::vector<WeakScalingResult> BenchmarkRunner::run_weak_scaling(
    size_t rows_per_thread,
    size_t max_threads,
    size_t num_portfolios
) {
    std::vector<WeakScalingResult> results;
    const std::vector<size_t> thread_counts = {1, 2, 4, 8, 16};

    double t1_time_ms = 0.0;

    for (size_t threads : thread_counts) {
        if (threads > max_threads) break;

        const size_t total_rows = threads * rows_per_thread;
        GeneratorConfig cfg;
        cfg.num_rows = total_rows;
        cfg.num_portfolios = num_portfolios;
        DataGenerator gen(cfg);

        ColumnarTable table;
        gen.generate_columnar(table);

        QueryEngine engine(threads, num_portfolios);
        QueryExecutionStats stats;

        // Warmup & benchmark
        engine.execute_risk_query(table, stats);
        engine.execute_risk_query(table, stats);

        const double time_ms = stats.total_engine_time_ms;
        if (threads == 1) {
            t1_time_ms = time_ms;
        }

        const double efficiency = (time_ms > 0.0) ? (t1_time_ms / time_ms) : 1.0;
        const double throughput = (time_ms > 0.0)
            ? (static_cast<double>(total_rows) / (time_ms / 1000.0) / 1'000'000.0)
            : 0.0;

        results.push_back({
            threads,
            total_rows,
            time_ms,
            efficiency,
            throughput
        });
    }

    return results;
}

ArchitecturalComparisonResult BenchmarkRunner::run_architectural_comparison(
    const std::vector<RawOptionRecord>& raw_records,
    const ColumnarTable& columnar_table,
    size_t threads,
    size_t num_portfolios,
    double network_bandwidth_gbps
) {
    ArchitecturalComparisonResult res;
    res.rows = raw_records.size();

    // 1. Traditional Architecture:
    // Network transfer time: moving raw rows across TCP/IP socket
    const size_t payload_bytes = raw_records.size() * sizeof(RawOptionRecord);
    const double bytes_per_sec = (network_bandwidth_gbps * 1e9) / 8.0;
    res.traditional_transfer_time_ms = (static_cast<double>(payload_bytes) / bytes_per_sec) * 1000.0;

    // Traditional application row-by-row compute
    const auto t_trad_start = Clock::now();
    std::vector<double> trad_port_deltas(num_portfolios, 0.0);
    std::vector<double> trad_port_values(num_portfolios, 0.0);

    for (size_t i = 0; i < raw_records.size(); ++i) {
        const auto& r = raw_records[i];
        if (r.is_valid()) {
            OptionGreeks g = compute_record_greeks(r);
            if (r.portfolio_id < num_portfolios) {
                trad_port_values[r.portfolio_id] += g.price * r.quantity;
                trad_port_deltas[r.portfolio_id] += g.delta * r.quantity;
            }
        }
    }
    const auto t_trad_end = Clock::now();
    res.traditional_compute_time_ms = std::chrono::duration<double, std::milli>(t_trad_end - t_trad_start).count();
    res.traditional_total_time_ms = res.traditional_transfer_time_ms + res.traditional_compute_time_ms;
    res.traditional_throughput_m_rows_sec = (res.traditional_total_time_ms > 0.0)
        ? (static_cast<double>(res.rows) / (res.traditional_total_time_ms / 1000.0) / 1'000'000.0)
        : 0.0;

    // 2. RiskDB Architecture (Zero-copy in-engine SIMD + OpenMP)
    QueryEngine engine(threads, num_portfolios);
    QueryExecutionStats stats;
    engine.execute_risk_query(columnar_table, stats);

    res.riskdb_compute_time_ms = stats.compute_time_ms;
    res.riskdb_reduction_time_ms = stats.reduction_time_ms;
    res.riskdb_total_time_ms = stats.total_engine_time_ms;
    res.riskdb_throughput_m_rows_sec = (res.riskdb_total_time_ms > 0.0)
        ? (static_cast<double>(res.rows) / (res.riskdb_total_time_ms / 1000.0) / 1'000'000.0)
        : 0.0;

    res.overall_speedup = (res.riskdb_total_time_ms > 0.0)
        ? (res.traditional_total_time_ms / res.riskdb_total_time_ms)
        : 1.0;

    return res;
}

void BenchmarkRunner::print_report(
    const std::vector<StrongScalingResult>& strong,
    const std::vector<WeakScalingResult>& weak,
    const ArchitecturalComparisonResult& arch
) {
    std::cout << "\n========================================================================================\n";
    std::cout << "                             RISKDB SYSTEM EVALUATION REPORT\n";
    std::cout << "========================================================================================\n\n";

    // 1. Strong Scaling Table
    std::cout << "--- 1. STRONG SCALING EVALUATION (Amdahl's Law, Fixed N = " << strong.front().rows << " rows) ---\n";
    std::cout << "+---------+----------------+--------------+--------------------+-------------------------+\n";
    std::cout << "| Threads | Execution Time | Speedup S(N) | Efficiency E(N)    | Throughput (M rows/sec) |\n";
    std::cout << "+---------+----------------+--------------+--------------------+-------------------------+\n";
    for (const auto& r : strong) {
        std::cout << "| " << std::setw(7) << r.threads << " | "
                  << std::setw(11) << std::fixed << std::setprecision(2) << r.time_ms << " ms | "
                  << std::setw(10) << std::setprecision(2) << r.speedup << "x | "
                  << std::setw(17) << std::setprecision(1) << (r.efficiency * 100.0) << "% | "
                  << std::setw(23) << std::setprecision(2) << r.throughput_m_rows_sec << " |\n";
    }
    std::cout << "+---------+----------------+--------------+--------------------+-------------------------+\n\n";

    // 2. Weak Scaling Table
    if (!weak.empty()) {
        std::cout << "--- 2. WEAK SCALING EVALUATION (Gustafson's Law, Scaled N proportional to Threads) ---\n";
        std::cout << "+---------+----------------+----------------+-----------------+-------------------------+\n";
        std::cout << "| Threads | Total Rows     | Execution Time | Efficiency E(N) | Throughput (M rows/sec) |\n";
        std::cout << "+---------+----------------+----------------+-----------------+-------------------------+\n";
        for (const auto& r : weak) {
            std::cout << "| " << std::setw(7) << r.threads << " | "
                      << std::setw(14) << r.rows << " | "
                      << std::setw(11) << std::fixed << std::setprecision(2) << r.time_ms << " ms | "
                      << std::setw(14) << std::setprecision(1) << (r.efficiency * 100.0) << "% | "
                      << std::setw(23) << std::setprecision(2) << r.throughput_m_rows_sec << " |\n";
        }
        std::cout << "+---------+----------------+----------------+-----------------+-------------------------+\n\n";
    }

    // 3. Architectural Comparison Table
    std::cout << "--- 3. ARCHITECTURAL COMPARISON (N = " << arch.rows << " contracts) ---\n";
    std::cout << "+---------------------------------------+--------------------------+---------------------+\n";
    std::cout << "| Architectural Metric                  | Traditional Tiered Stack | RiskDB Architecture |\n";
    std::cout << "+---------------------------------------+--------------------------+---------------------+\n";
    std::cout << "| Network/Serialization Overhead        | " << std::setw(21) << std::fixed << std::setprecision(2) << arch.traditional_transfer_time_ms << " ms | "
              << std::setw(19) << "0.00 ms (Zero-Copy)" << " |\n";
    std::cout << "| Execution/Compute Time                | " << std::setw(21) << std::fixed << std::setprecision(2) << arch.traditional_compute_time_ms << " ms | "
              << std::setw(19) << std::setprecision(2) << arch.riskdb_compute_time_ms << " ms |\n";
    std::cout << "| Reduction/Aggregation Merge Time      | " << std::setw(21) << "Included (Sequential)" << " | "
              << std::setw(19) << std::setprecision(2) << arch.riskdb_reduction_time_ms << " ms |\n";
    std::cout << "| Total End-to-End Latency              | " << std::setw(21) << std::fixed << std::setprecision(2) << arch.traditional_total_time_ms << " ms | "
              << std::setw(19) << std::setprecision(2) << arch.riskdb_total_time_ms << " ms |\n";
    std::cout << "| Processing Throughput                 | " << std::setw(19) << std::fixed << std::setprecision(2) << arch.traditional_throughput_m_rows_sec << " M/sec | "
              << std::setw(17) << std::setprecision(2) << arch.riskdb_throughput_m_rows_sec << " M/sec |\n";
    std::cout << "+---------------------------------------+--------------------------+---------------------+\n";
    std::cout << "| OVERALL SPEEDUP FACTOR                |                     1.00 | "
              << std::setw(17) << std::setprecision(2) << arch.overall_speedup << "x |\n";
    std::cout << "+---------------------------------------+--------------------------+---------------------+\n\n";
}

} // namespace riskdb
