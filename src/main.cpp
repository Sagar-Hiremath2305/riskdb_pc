#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include "common/types.hpp"
#include "columnar/columnar_table.hpp"
#include "columnar/columnar_pivot.hpp"
#include "storage/data_generator.hpp"
#include "storage/postgres_connector.hpp"
#include "engine/query_engine.hpp"
#include "benchmark/benchmark_runner.hpp"

#if defined(_OPENMP)
#include <omp.h>
#endif

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [OPTIONS]\n"
              << "Options:\n"
              << "  --benchmark [N]           Run full academic benchmark suite with N rows (default: 1,000,000, up to 10M)\n"
              << "  --strong-scaling [N]      Run strong scaling evaluation (1 to 16 threads)\n"
              << "  --weak-scaling [N/thread] Run weak scaling evaluation (e.g. 500000 rows/thread)\n"
              << "  --compare [N]             Run architectural comparison (Traditional vs RiskDB)\n"
              << "  --generate-csv <path> <N> Generate synthetic dataset to CSV file for PostgreSQL ingestion\n"
              << "  --from-pg <conninfo>      Query contracts from PostgreSQL and run in-engine acceleration\n"
              << "  --threads <T>             Set worker thread count (default: max available, up to 16)\n"
              << "  --portfolios <P>          Set portfolio count (default: 100, range 50-500)\n"
              << "  --help                    Show this help message\n";
}

int main(int argc, char* argv[]) {
    size_t num_rows = 1'000'000;
    size_t num_portfolios = 100;
    size_t thread_count = 0; // 0 = auto-detect

#if defined(_OPENMP)
    thread_count = static_cast<size_t>(omp_get_max_threads());
#else
    thread_count = 4;
#endif
    if (thread_count > 16) thread_count = 16;

    std::string mode = "benchmark";
    std::string csv_path;
    std::string pg_conninfo;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--benchmark") {
            mode = "benchmark";
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                num_rows = std::stoull(argv[++i]);
            }
        } else if (arg == "--strong-scaling") {
            mode = "strong";
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                num_rows = std::stoull(argv[++i]);
            }
        } else if (arg == "--weak-scaling") {
            mode = "weak";
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                num_rows = std::stoull(argv[++i]);
            }
        } else if (arg == "--compare") {
            mode = "compare";
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                num_rows = std::stoull(argv[++i]);
            }
        } else if (arg == "--generate-csv") {
            mode = "csv";
            if (i + 1 < argc) csv_path = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                num_rows = std::stoull(argv[++i]);
            }
        } else if (arg == "--from-pg") {
            mode = "pg";
            if (i + 1 < argc) pg_conninfo = argv[++i];
        } else if (arg == "--threads") {
            if (i + 1 < argc) thread_count = std::stoull(argv[++i]);
        } else if (arg == "--portfolios") {
            if (i + 1 < argc) num_portfolios = std::stoull(argv[++i]);
        }
    }

    std::cout << "========================================================================================\n"
              << "                 RiskDB: High-Performance Parallel Options Query Engine                \n"
              << "========================================================================================\n"
              << " Hardware Concurrency: " << thread_count << " threads\n"
              << " Active SIMD: "
#if defined(__AVX2__)
              << "Intel/AMD AVX2 (256-bit registers, 8 single-precision FLOPs/cycle)\n"
#elif defined(__ARM_NEON)
              << "ARM NEON (128-bit registers, 4 single-precision FLOPs/cycle)\n"
#else
              << "Portable Vectorized Fallback\n"
#endif
              << " Memory Alignment: 64-byte Cache Aligned (Zero False-Sharing)\n"
              << " Concurrency Model: OpenMP + Lock-Free Thread-Local Accumulators + O(log T) Reduction\n"
              << "========================================================================================\n\n";

    if (mode == "csv") {
        std::cout << "[*] Generating synthetic dataset of " << num_rows << " contracts to " << csv_path << "...\n";
        riskdb::GeneratorConfig cfg;
        cfg.num_rows = num_rows;
        cfg.num_portfolios = num_portfolios;
        riskdb::DataGenerator gen(cfg);
        const size_t written = gen.export_to_csv(csv_path, num_rows);
        std::cout << "[+] Successfully exported " << written << " rows to CSV.\n";
        return 0;
    }

    if (mode == "pg") {
        std::cout << "[*] Connecting to PostgreSQL database: " << pg_conninfo << "...\n";
        riskdb::PostgresConfig pg_cfg;
        // Parse host, port, dbname from conninfo or use defaults
        riskdb::PostgresConnector connector(pg_cfg);
        riskdb::ColumnarTable table;
        std::cout << "[*] Fetching contracts directly into cache-aligned ColumnarTable...\n";
        size_t count = connector.fetch_to_columnar(table);
        if (count == 0) {
            std::cerr << "[-] Error or zero rows fetched: " << connector.last_error() << "\n";
            return 1;
        }
        std::cout << "[+] Ingested " << count << " contracts. Executing parallel SIMD risk query...\n";
        riskdb::QueryEngine engine(thread_count, num_portfolios);
        riskdb::QueryExecutionStats stats;
        auto results = engine.execute_risk_query(table, stats);
        std::cout << "[+] Completed in " << stats.total_engine_time_ms << " ms. Throughput: "
                  << (stats.throughput_rows_per_sec / 1e6) << " M rows/sec.\n";
        return 0;
    }

    // Default benchmark modes
    std::cout << "[*] Synthesizing " << num_rows << " options contracts (" << num_portfolios << " portfolios)...\n";
    riskdb::GeneratorConfig cfg;
    cfg.num_rows = num_rows;
    cfg.num_portfolios = num_portfolios;
    riskdb::DataGenerator gen(cfg);

    const auto raw_rows = gen.generate_rows();
    riskdb::ColumnarTable columnar_table;
    riskdb::ColumnarPivot::pivot(raw_rows, columnar_table);
    std::cout << "[+] Data pivoting complete. Columnar footprint: "
              << (columnar_table.memory_footprint_bytes() / (1024.0 * 1024.0)) << " MB.\n";

    if (mode == "strong" || mode == "benchmark") {
        std::cout << "[*] Running Strong Scaling benchmark (Amdahl's Law)...\n";
        auto strong_res = riskdb::BenchmarkRunner::run_strong_scaling(columnar_table, thread_count, num_portfolios);

        std::vector<riskdb::WeakScalingResult> weak_res;
        if (mode == "benchmark") {
            std::cout << "[*] Running Weak Scaling benchmark (Gustafson's Law)...\n";
            weak_res = riskdb::BenchmarkRunner::run_weak_scaling(std::min(num_rows, static_cast<size_t>(500'000)), thread_count, num_portfolios);
        }

        std::cout << "[*] Running Architectural Comparison (Traditional vs RiskDB)...\n";
        auto arch_res = riskdb::BenchmarkRunner::run_architectural_comparison(raw_rows, columnar_table, thread_count, num_portfolios);

        riskdb::BenchmarkRunner::print_report(strong_res, weak_res, arch_res);
    } else if (mode == "weak") {
        std::cout << "[*] Running Weak Scaling benchmark (Gustafson's Law)...\n";
        auto weak_res = riskdb::BenchmarkRunner::run_weak_scaling(num_rows, thread_count, num_portfolios);
        std::vector<riskdb::StrongScalingResult> dummy_strong;
        riskdb::ArchitecturalComparisonResult dummy_arch{};
        riskdb::BenchmarkRunner::print_report(dummy_strong, weak_res, dummy_arch);
    } else if (mode == "compare") {
        std::cout << "[*] Running Architectural Comparison...\n";
        auto arch_res = riskdb::BenchmarkRunner::run_architectural_comparison(raw_rows, columnar_table, thread_count, num_portfolios);
        std::vector<riskdb::StrongScalingResult> dummy_strong;
        std::vector<riskdb::WeakScalingResult> dummy_weak;
        riskdb::BenchmarkRunner::print_report(dummy_strong, dummy_weak, arch_res);
    }

    return 0;
}
