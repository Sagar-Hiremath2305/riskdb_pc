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

    std::cout << "[*] Pre-computing and storing initial option prices and Greeks in database memory...\n";
    table.recompute_stored_metrics(-1);
    std::cout << "[+] In-database options prices and Greeks initialized.\n";

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

    static std::atomic<uint64_t> riskdb_update_ver{1};
    static std::atomic<double> riskdb_last_compute_ms{2.1};
    static std::atomic<uint32_t> riskdb_last_underlying{5};

    // Endpoint: Status
    server.route("GET", "/api/status", [&table, thread_count](const riskdb::HttpRequest&) {
        std::ostringstream ss;
        ss << "{\n"
           << "  \"status\": \"healthy\",\n"
           << "  \"engine\": \"RiskDB Parallel SIMD Engine\",\n"
           << "  \"update_version\": " << riskdb_update_ver.load() << ",\n"
           << "  \"last_underlying_id\": " << riskdb_last_underlying.load() << ",\n"
           << "  \"last_compute_ms\": " << riskdb_last_compute_ms.load() << ",\n"
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

    // Endpoint: Contracts & Greeks Query for Client Application
    server.route("GET", "/api/contracts", [&table](const riskdb::HttpRequest& req) {
        size_t limit = 50;
        size_t offset = 0;
        int underlying_filter = -1;
        int portfolio_filter = -1;

        if (!req.get_param("limit").empty()) limit = std::stoull(req.get_param("limit"));
        if (!req.get_param("offset").empty()) offset = std::stoull(req.get_param("offset"));
        if (!req.get_param("underlying").empty()) underlying_filter = std::stoi(req.get_param("underlying"));
        if (!req.get_param("portfolio").empty()) portfolio_filter = std::stoi(req.get_param("portfolio"));

        if (limit > 500) limit = 500;

        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4);
        ss << "{\n";
        ss << "  \"total_stored_contracts\": " << table.size() << ",\n";
        ss << "  \"update_version\": " << riskdb_update_ver.load() << ",\n";
        ss << "  \"last_underlying_id\": " << riskdb_last_underlying.load() << ",\n";
        ss << "  \"last_compute_ms\": " << riskdb_last_compute_ms.load() << ",\n";
        ss << "  \"total_latency_ms\": " << riskdb_last_compute_ms.load() << ",\n";
        ss << "  \"contracts\": [\n";

        size_t matches = 0;
        size_t written = 0;

        const float* spots = table.spot_prices();
        const float* strikes = table.strike_prices();
        const float* expiries = table.times_to_expiry();
        const float* vols = table.volatilities();
        const float* qtys = table.quantities();
        const uint8_t* types = table.option_types();
        const uint16_t* ports = table.portfolio_ids();
        const uint32_t* underlyings = table.underlying_ids();
        const uint64_t* contract_ids = table.contract_ids();

        const float* c_prices = table.computed_prices();
        const float* c_deltas = table.computed_deltas();
        const float* c_gammas = table.computed_gammas();
        const float* c_vegas  = table.computed_vegas();
        const float* c_thetas = table.computed_thetas();

        for (size_t i = 0; i < table.size(); ++i) {
            if (underlying_filter >= 0 && underlyings[i] != static_cast<uint32_t>(underlying_filter)) continue;
            if (portfolio_filter >= 0 && ports[i] != static_cast<uint16_t>(portfolio_filter)) continue;

            if (matches >= offset && written < limit) {
                if (written > 0) ss << ",\n";
                ss << "    {"
                   << "\"contract_id\": " << contract_ids[i] << ", "
                   << "\"underlying_id\": " << underlyings[i] << ", "
                   << "\"portfolio_id\": " << ports[i] << ", "
                   << "\"option_type\": \"" << (types[i] == 0 ? "CALL" : "PUT") << "\", "
                   << "\"spot_price\": " << spots[i] << ", "
                   << "\"strike_price\": " << strikes[i] << ", "
                   << "\"time_to_expiry\": " << expiries[i] << ", "
                   << "\"volatility\": " << vols[i] << ", "
                   << "\"quantity\": " << qtys[i] << ", "
                   << "\"option_price\": " << c_prices[i] << ", "
                   << "\"delta\": " << c_deltas[i] << ", "
                   << "\"gamma\": " << c_gammas[i] << ", "
                   << "\"vega\": " << c_vegas[i] << ", "
                   << "\"theta\": " << c_thetas[i]
                   << "}";
                ++written;
            }
            ++matches;
            if (written >= limit) break;
        }

        ss << "\n  ],\n";
        ss << "  \"matched_count\": " << matches << "\n";
        ss << "}\n";
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

    // Endpoint: In-Engine Market Signal Processing (Price Changed Signal)
    auto handle_signal = [&table, &num_portfolios](const riskdb::HttpRequest& req) {
        uint32_t underlying_id = 1;
        float new_spot_price = 100.0f;
        size_t threads = 8;

        if (!req.get_param("underlying").empty()) {
            underlying_id = std::stoul(req.get_param("underlying"));
        }
        if (!req.get_param("spot").empty()) {
            new_spot_price = std::stof(req.get_param("spot"));
        }
        if (!req.get_param("threads").empty()) {
            threads = std::stoull(req.get_param("threads"));
        }

        if (!req.body.empty()) {
            size_t u_pos = req.body.find("\"underlying_id\":");
            if (u_pos != std::string::npos) {
                underlying_id = std::stoul(req.body.substr(u_pos + 16));
            }
            size_t s_pos = req.body.find("\"spot_price\":");
            if (s_pos != std::string::npos) {
                new_spot_price = std::stof(req.body.substr(s_pos + 13));
            }
        }

        if (threads == 0) threads = 1;
        if (threads > 16) threads = 16;

        const auto t_start = Clock::now();

        // 1. In-place columnar update of spot price in database memory
        const auto t_update_start = Clock::now();
        size_t affected = table.update_underlying_spot(underlying_id, new_spot_price);
        const auto t_update_end = Clock::now();
        const double update_ms = std::chrono::duration<double, std::milli>(t_update_end - t_update_start).count();

        // 2. In-Engine Parallel SIMD Vector Execution & Reduction
        riskdb::QueryEngine engine(threads, num_portfolios);
        riskdb::QueryExecutionStats stats;
        auto results = engine.execute_risk_query(table, stats);
        const auto t_end = Clock::now();

        const double total_process_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

        std::cout << "[SIGNAL RECEIVED] Underlying #" << underlying_id << " Spot Price -> $"
                  << std::fixed << std::setprecision(2) << new_spot_price
                  << " | Contracts updated in-engine: " << affected << "\n"
                  << "                 -> In-Place Memory Update: " << std::setprecision(3) << update_ms << " ms\n"
                  << "                 -> Parallel SIMD Compute: " << stats.compute_time_ms << " ms\n"
                  << "                 -> O(log T) Reduction Tree: " << stats.reduction_time_ms << " ms\n"
                  << "                 -> Total Process Completion Time: " << total_process_ms << " ms\n"
                  << "                 -> Engine Throughput: " << (stats.throughput_rows_per_sec / 1e6) << " M rows/sec\n\n";

        riskdb_last_compute_ms.store(total_process_ms);
        riskdb_last_underlying.store(underlying_id);
        riskdb_update_ver.fetch_add(1);

        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4);
        ss << "{\n";
        ss << "  \"signal_status\": \"PROCESSED_IN_ENGINE\",\n";
        ss << "  \"underlying_id\": " << underlying_id << ",\n";
        ss << "  \"new_spot_price\": " << new_spot_price << ",\n";
        ss << "  \"contracts_affected\": " << affected << ",\n";
        ss << "  \"signal_payload_bytes\": 32,\n";
        ss << "  \"timing_breakdown\": {\n";
        ss << "    \"in_place_update_time_ms\": " << update_ms << ",\n";
        ss << "    \"simd_compute_time_ms\": " << stats.compute_time_ms << ",\n";
        ss << "    \"reduction_time_ms\": " << stats.reduction_time_ms << ",\n";
        ss << "    \"total_engine_process_time_ms\": " << total_process_ms << ",\n";
        ss << "    \"throughput_m_rows_sec\": " << (stats.throughput_rows_per_sec / 1e6) << "\n";
        ss << "  },\n";

        double total_val = 0.0, total_del = 0.0, total_gam = 0.0, total_veg = 0.0;
        uint64_t total_cnt = 0;
        for (const auto& m : results) {
            total_val += m.total_value;
            total_del += m.total_delta;
            total_gam += m.total_gamma;
            total_veg += m.total_vega;
            total_cnt += m.contract_count;
        }

        ss << "  \"summary\": {\n";
        ss << "    \"portfolio_count\": " << results.size() << ",\n";
        ss << "    \"total_contracts\": " << total_cnt << ",\n";
        ss << "    \"total_portfolio_value\": " << total_val << ",\n";
        ss << "    \"total_portfolio_delta\": " << total_del << ",\n";
        ss << "    \"total_portfolio_gamma\": " << total_gam << ",\n";
        ss << "    \"total_portfolio_vega\": " << total_veg << "\n";
        ss << "  },\n";
        ss << "  \"portfolios\": [\n";
        for (size_t i = 0; i < results.size(); ++i) {
            const auto& m = results[i];
            ss << "    {\"portfolio_id\": " << m.portfolio_id
               << ", \"contracts\": " << m.contract_count
               << ", \"total_value\": " << m.total_value
               << ", \"delta\": " << m.total_delta
               << ", \"gamma\": " << m.total_gamma
               << ", \"vega\": " << m.total_vega << "}"
               << (i + 1 < results.size() ? ",\n" : "\n");
        }
        ss << "  ]\n";
        ss << "}\n";

        return riskdb::HttpResponse::json(ss.str());
    };

    server.route("POST", "/api/signal", handle_signal);
    server.route("GET", "/api/signal", handle_signal);

    // Endpoint: Full Academic Benchmark for UI Visualizer (Strong & Weak Scaling + Architectural Comparison)
    server.route("GET", "/api/benchmark", [&table, num_portfolios](const riskdb::HttpRequest& req) {
        size_t threads = std::stoull(req.get_param("threads", "8"));
        if (threads == 0) threads = 8;
        if (threads > 16) threads = 16;

        auto strong_res = riskdb::BenchmarkRunner::run_strong_scaling(table, threads, num_portfolios);
        auto weak_res = riskdb::BenchmarkRunner::run_weak_scaling(std::min(table.size(), static_cast<size_t>(500'000)), threads, num_portfolios);

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
        ss << "  \"weak_scaling\": [\n";
        for (size_t i = 0; i < weak_res.size(); ++i) {
            const auto& r = weak_res[i];
            ss << "    {\"threads\": " << r.threads
               << ", \"rows\": " << r.rows
               << ", \"time_ms\": " << r.time_ms
               << ", \"efficiency\": " << (r.efficiency * 100.0)
               << ", \"throughput_m_rows_sec\": " << r.throughput_m_rows_sec << "}"
               << (i + 1 < weak_res.size() ? ",\n" : "\n");
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
