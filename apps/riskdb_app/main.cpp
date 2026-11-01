#include <iostream>
#include <chrono>
#include <vector>
#include <string>
#include <iomanip>
#include <sstream>
#include <thread>
#include "columnar/columnar_table.hpp"
#include "columnar/columnar_pivot.hpp"
#include "storage/postgres_connector.hpp"
#include "storage/data_generator.hpp"
#include "engine/query_engine.hpp"
#include "benchmark/benchmark_runner.hpp"
#include "server/http_server.hpp"

#if defined(_OPENMP)
#include <omp.h>
#endif

using Clock = std::chrono::high_resolution_clock;

std::string format_risk_metrics_json(
    const std::vector<riskdb::PortfolioRiskMetrics>& metrics,
    const riskdb::QueryExecutionStats& stats,
    double total_time_ms
) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(4);
    ss << "{\n";
    ss << "  \"engine\": \"RiskDB Accelerated Parallel Engine\",\n";
    ss << "  \"architecture\": \"In-Memory 64-byte Aligned Columnar + SIMD + OpenMP\",\n";
    ss << "  \"stats\": {\n";
    ss << "    \"rows_processed\": " << stats.rows_processed << ",\n";
    ss << "    \"thread_count\": " << stats.thread_count << ",\n";
    ss << "    \"compute_time_ms\": " << stats.compute_time_ms << ",\n";
    ss << "    \"reduction_time_ms\": " << stats.reduction_time_ms << ",\n";
    ss << "    \"total_engine_time_ms\": " << stats.total_engine_time_ms << ",\n";
    ss << "    \"roundtrip_time_ms\": " << total_time_ms << ",\n";
    ss << "    \"throughput_m_rows_sec\": " << (stats.throughput_rows_per_sec / 1'000'000.0) << "\n";
    ss << "  },\n";

    // Summary totals across all portfolios
    double total_val = 0.0, total_del = 0.0, total_gam = 0.0, total_veg = 0.0;
    uint64_t total_cnt = 0;
    for (const auto& m : metrics) {
        total_val += m.total_value;
        total_del += m.total_delta;
        total_gam += m.total_gamma;
        total_veg += m.total_vega;
        total_cnt += m.contract_count;
    }

    ss << "  \"summary\": {\n";
    ss << "    \"portfolio_count\": " << metrics.size() << ",\n";
    ss << "    \"total_contracts\": " << total_cnt << ",\n";
    ss << "    \"total_portfolio_value\": " << total_val << ",\n";
    ss << "    \"total_portfolio_delta\": " << total_del << ",\n";
    ss << "    \"total_portfolio_gamma\": " << total_gam << ",\n";
    ss << "    \"total_portfolio_vega\": " << total_veg << "\n";
    ss << "  },\n";

    ss << "  \"portfolios\": [\n";
    for (size_t i = 0; i < metrics.size(); ++i) {
        const auto& m = metrics[i];
        ss << "    {\"portfolio_id\": " << m.portfolio_id
           << ", \"contracts\": " << m.contract_count
           << ", \"total_value\": " << m.total_value
           << ", \"delta\": " << m.total_delta
           << ", \"gamma\": " << m.total_gamma
           << ", \"vega\": " << m.total_vega << "}"
           << (i + 1 < metrics.size() ? ",\n" : "\n");
    }
    ss << "  ]\n";
    ss << "}\n";
    return ss.str();
}

int main(int argc, char* argv[]) {
    int port = 8080;
    size_t default_rows = 1'000'000;
    size_t num_portfolios = 100;
    size_t thread_count = 16;
    bool run_cli = false;
    std::string web_dir = "./web";

#if defined(_OPENMP)
    thread_count = static_cast<size_t>(omp_get_max_threads());
#endif
    if (thread_count > 16) thread_count = 16;

    riskdb::PostgresConfig pg_cfg;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) port = std::stoi(argv[++i]);
        else if (arg == "--threads" && i + 1 < argc) thread_count = std::stoull(argv[++i]);
        else if (arg == "--rows" && i + 1 < argc) default_rows = std::stoull(argv[++i]);
        else if (arg == "--portfolios" && i + 1 < argc) num_portfolios = std::stoull(argv[++i]);
        else if (arg == "--host" && i + 1 < argc) pg_cfg.host = argv[++i];
        else if (arg == "--dbname" && i + 1 < argc) pg_cfg.dbname = argv[++i];
        else if (arg == "--user" && i + 1 < argc) pg_cfg.user = argv[++i];
        else if (arg == "--password" && i + 1 < argc) pg_cfg.password = argv[++i];
        else if (arg == "--web-dir" && i + 1 < argc) web_dir = argv[++i];
        else if (arg == "--cli") run_cli = true;
    }

    std::cout << "========================================================================================\n"
              << "       APPLICATION 2: RiskDB Mathematical Computation Engine Service                    \n"
              << "========================================================================================\n"
              << " Role: High-Performance Parallel Vectorized Query Engine Server over PostgreSQL         \n"
              << " Concurrency: OpenMP (" << thread_count << " worker threads)                            \n"
              << " Hardware SIMD: "
#if defined(__AVX2__)
              << "Intel/AMD AVX2 (256-bit registers, 8 single-precision FLOPs/cycle)\n"
#elif defined(__ARM_NEON)
              << "ARM NEON (128-bit registers, 4 single-precision FLOPs/cycle)\n"
#else
              << "Portable Vectorized Fallback\n"
#endif
              << " Reduction: O(log T) Pairwise SIMD Parallel Merging                                     \n"
              << " HTTP REST Service Port: " << port << "\n"
              << "========================================================================================\n\n";

    // Initialize in-memory cache-aligned columnar table
    riskdb::ColumnarTable table;
    riskdb::PostgresConnector connector(pg_cfg);

    std::cout << "[*] Pre-loading contracts dataset...\n";
    if (connector.connect()) {
        std::cout << "[+] Connected to PostgreSQL. Streaming contracts into cache-aligned columnar memory...\n";
        connector.fetch_to_columnar(table);
    }

    if (table.size() == 0) {
        std::cout << "[!] Generating in-memory options portfolio dataset (" << default_rows << " contracts, "
                  << num_portfolios << " portfolios)...\n";
        riskdb::GeneratorConfig gen_cfg;
        gen_cfg.num_rows = default_rows;
        gen_cfg.num_portfolios = num_portfolios;
        riskdb::DataGenerator gen(gen_cfg);
        gen.generate_columnar(table);
    }

    std::cout << "[+] Columnar store active: " << table.size() << " contracts ("
              << (table.memory_footprint_bytes() / (1024.0 * 1024.0)) << " MB)\n";

    if (run_cli) {
        // One-off CLI run
        riskdb::QueryEngine engine(thread_count, num_portfolios);
        riskdb::QueryExecutionStats stats;
        auto results = engine.execute_risk_query(table, stats);
        std::cout << "[+] Execution complete in " << stats.total_engine_time_ms << " ms. Throughput: "
                  << (stats.throughput_rows_per_sec / 1e6) << " M rows/sec.\n";
        return 0;
    }

    // Launch Mathematical Computation Engine HTTP REST Service
    riskdb::HttpServer server(port);
    server.set_static_dir(web_dir);

    // Endpoint: Status
    server.route("GET", "/api/status", [&table, thread_count](const riskdb::HttpRequest&) {
        std::ostringstream ss;
        ss << "{\n"
           << "  \"status\": \"healthy\",\n"
           << "  \"engine\": \"RiskDB Parallel SIMD Engine\",\n"
           << "  \"simd_architecture\": \""
#if defined(__AVX2__)
           << "AVX2 (256-bit)"
#elif defined(__ARM_NEON)
           << "ARM NEON (128-bit)"
#else
           << "Portable"
#endif
           << "\",\n"
           << "  \"cached_contracts\": " << table.size() << ",\n"
           << "  \"worker_threads\": " << thread_count << "\n"
           << "}\n";
        return riskdb::HttpResponse::json(ss.str());
    });

    // Endpoint: Compute Risk (Parallel SIMD Query)
    server.route("GET", "/api/risk", [&table, &num_portfolios](const riskdb::HttpRequest& req) {
        size_t threads = std::stoull(req.get_param("threads", "8"));
        if (threads == 0) threads = 1;
        if (threads > 16) threads = 16;

        const auto t_start = Clock::now();
        riskdb::QueryEngine engine(threads, num_portfolios);
        riskdb::QueryExecutionStats stats;
        auto results = engine.execute_risk_query(table, stats);
        const auto t_end = Clock::now();

        const double total_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
        std::string json = format_risk_metrics_json(results, stats, total_ms);
        return riskdb::HttpResponse::json(json);
    });

    // Endpoint: Full Academic Benchmark for UI Visualizer
    server.route("GET", "/api/benchmark", [&table, num_portfolios](const riskdb::HttpRequest& req) {
        size_t threads = std::stoull(req.get_param("threads", "8"));
        if (threads == 0) threads = 8;
        if (threads > 16) threads = 16;

        auto strong_res = riskdb::BenchmarkRunner::run_strong_scaling(table, threads, num_portfolios);

        // Generate matching raw records for architectural comparison
        riskdb::GeneratorConfig cfg;
        cfg.num_rows = table.size();
        cfg.num_portfolios = num_portfolios;
        riskdb::DataGenerator gen(cfg);
        auto raw = gen.generate_rows();

        auto arch_res = riskdb::BenchmarkRunner::run_architectural_comparison(raw, table, threads, num_portfolios);

        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);
        ss << "{\n";
        ss << "  \"strong_scaling\": [\n";
        for (size_t i = 0; i < strong_res.size(); ++i) {
            const auto& r = strong_res[i];
            ss << "    {\"threads\": " << r.threads
               << ", \"time_ms\": " << r.time_ms
               << ", \"speedup\": " << r.speedup
               << ", \"efficiency\": " << (r.efficiency * 100.0)
               << ", \"throughput_m_rows_sec\": " << r.throughput_m_rows_sec << "}"
               << (i + 1 < strong_res.size() ? ",\n" : "\n");
        }
        ss << "  ],\n";
        ss << "  \"architectural_comparison\": {\n";
        ss << "    \"rows\": " << arch_res.rows << ",\n";
        ss << "    \"traditional_transfer_time_ms\": " << arch_res.traditional_transfer_time_ms << ",\n";
        ss << "    \"traditional_compute_time_ms\": " << arch_res.traditional_compute_time_ms << ",\n";
        ss << "    \"traditional_total_time_ms\": " << arch_res.traditional_total_time_ms << ",\n";
        ss << "    \"traditional_throughput\": " << arch_res.traditional_throughput_m_rows_sec << ",\n";
        ss << "    \"riskdb_compute_time_ms\": " << arch_res.riskdb_compute_time_ms << ",\n";
        ss << "    \"riskdb_reduction_time_ms\": " << arch_res.riskdb_reduction_time_ms << ",\n";
        ss << "    \"riskdb_total_time_ms\": " << arch_res.riskdb_total_time_ms << ",\n";
        ss << "    \"riskdb_throughput\": " << arch_res.riskdb_throughput_m_rows_sec << ",\n";
        ss << "    \"speedup\": " << arch_res.overall_speedup << "\n";
        ss << "  }\n";
        ss << "}\n";

        return riskdb::HttpResponse::json(ss.str());
    });

    if (!server.start()) {
        std::cerr << "[-] Failed to start HTTP server\n";
        return 1;
    }

    std::cout << "[+] RiskDB Mathematical Computation Engine active on port " << port << "!\n";
    std::cout << "[+] Open Web Dashboard at: http://localhost:" << port << "\n";
    std::cout << "[*] Press Ctrl+C to terminate.\n";

    while (server.is_running()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}
