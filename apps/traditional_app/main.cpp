#include <iostream>
#include <chrono>
#include <vector>
#include <string>
#include <iomanip>
#include <sstream>
#include <thread>
#include "storage/postgres_connector.hpp"
#include "storage/data_generator.hpp"
#include "common/math_utils.hpp"
#include "common/types.hpp"
#include "server/http_server.hpp"

using Clock = std::chrono::high_resolution_clock;

std::string format_traditional_metrics_json(
    const std::vector<riskdb::PortfolioRiskMetrics>& metrics,
    double fetch_ms,
    double comp_ms,
    size_t rows_processed
) {
    const double total_ms = fetch_ms + comp_ms;
    const double throughput = (total_ms > 0.0) ? (static_cast<double>(rows_processed) / (total_ms / 1000.0) / 1'000'000.0) : 0.0;

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(4);
    ss << "{\n";
    ss << "  \"engine\": \"Traditional Tiered Database Architecture\",\n";
    ss << "  \"architecture\": \"External Application Loop over PostgreSQL Tuples\",\n";
    ss << "  \"stats\": {\n";
    ss << "    \"rows_processed\": " << rows_processed << ",\n";
    ss << "    \"thread_count\": 1,\n";
    ss << "    \"network_fetch_time_ms\": " << fetch_ms << ",\n";
    ss << "    \"application_compute_time_ms\": " << comp_ms << ",\n";
    ss << "    \"total_time_ms\": " << total_ms << ",\n";
    ss << "    \"throughput_m_rows_sec\": " << throughput << "\n";
    ss << "  },\n";

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
    int port = 8081;
    riskdb::PostgresConfig pg_cfg;
    size_t fallback_rows = 1'000'000;
    size_t num_portfolios = 100;
    bool run_cli = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) port = std::stoi(argv[++i]);
        else if (arg == "--host" && i + 1 < argc) pg_cfg.host = argv[++i];
        else if (arg == "--dbname" && i + 1 < argc) pg_cfg.dbname = argv[++i];
        else if (arg == "--user" && i + 1 < argc) pg_cfg.user = argv[++i];
        else if (arg == "--password" && i + 1 < argc) pg_cfg.password = argv[++i];
        else if (arg == "--rows" && i + 1 < argc) fallback_rows = std::stoull(argv[++i]);
        else if (arg == "--portfolios" && i + 1 < argc) num_portfolios = std::stoull(argv[++i]);
        else if (arg == "--cli") run_cli = true;
    }

    std::cout << "========================================================================================\n"
              << "       APPLICATION 1: Traditional Tiered Architecture Service (VM 1)                    \n"
              << "========================================================================================\n"
              << " Role: External Application Fetching Rows from PostgreSQL & Scalar Loop                 \n"
              << " HTTP REST Service Port: " << port << "\n"
              << "========================================================================================\n\n";

    std::vector<riskdb::RawOptionRecord> cached_records;
    riskdb::PostgresConnector connector(pg_cfg);

    if (connector.connect()) {
        std::cout << "[+] Connected to PostgreSQL. Streaming contracts into memory...\n";
        cached_records = connector.fetch_rows();
    }

    if (cached_records.empty()) {
        std::cout << "[!] PostgreSQL table empty. Pre-generating synthetic dataset (" << fallback_rows << " rows)...\n";
        riskdb::GeneratorConfig gen_cfg;
        gen_cfg.num_rows = fallback_rows;
        gen_cfg.num_portfolios = num_portfolios;
        riskdb::DataGenerator gen(gen_cfg);
        cached_records = gen.generate_rows();
    }

    auto compute_traditional = [&cached_records, num_portfolios](double& fetch_ms, double& comp_ms) {
        // Network payload transfer simulation (10 Gbps)
        const size_t payload_bytes = cached_records.size() * sizeof(riskdb::RawOptionRecord);
        constexpr double bytes_per_sec = (10.0 * 1e9) / 8.0;
        fetch_ms = (static_cast<double>(payload_bytes) / bytes_per_sec) * 1000.0;

        const auto t_start = Clock::now();
        std::vector<riskdb::PortfolioRiskMetrics> metrics(num_portfolios);
        for (size_t p = 0; p < num_portfolios; ++p) {
            metrics[p] = riskdb::PortfolioRiskMetrics(static_cast<uint32_t>(p));
        }

        for (size_t i = 0; i < cached_records.size(); ++i) {
            const auto& r = cached_records[i];
            if (r.is_valid()) {
                riskdb::OptionGreeks g = riskdb::compute_record_greeks(r);
                if (r.portfolio_id < num_portfolios) {
                    metrics[r.portfolio_id].total_value += g.price * r.quantity;
                    metrics[r.portfolio_id].total_delta += g.delta * r.quantity;
                    metrics[r.portfolio_id].total_gamma += g.gamma * r.quantity;
                    metrics[r.portfolio_id].total_vega  += g.vega  * r.quantity;
                    metrics[r.portfolio_id].contract_count += 1;
                }
            }
        }
        const auto t_end = Clock::now();
        comp_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
        return metrics;
    };

    if (run_cli) {
        double f_ms = 0.0, c_ms = 0.0;
        auto metrics = compute_traditional(f_ms, c_ms);
        std::cout << "[+] Traditional compute completed in " << (f_ms + c_ms) << " ms ("
                  << f_ms << " ms network + " << c_ms << " ms CPU)\n";
        return 0;
    }

    riskdb::HttpServer server(port);

    server.route("GET", "/api/status", [&cached_records](const riskdb::HttpRequest&) {
        std::ostringstream ss;
        ss << "{\n"
           << "  \"status\": \"healthy\",\n"
           << "  \"engine\": \"Traditional Tiered Stack\",\n"
           << "  \"cached_contracts\": " << cached_records.size() << "\n"
           << "}\n";
        return riskdb::HttpResponse::json(ss.str());
    });

    server.route("GET", "/api/risk", [&compute_traditional, &cached_records](const riskdb::HttpRequest&) {
        double f_ms = 0.0, c_ms = 0.0;
        auto metrics = compute_traditional(f_ms, c_ms);
        std::string json = format_traditional_metrics_json(metrics, f_ms, c_ms, cached_records.size());
        return riskdb::HttpResponse::json(json);
    });

    if (!server.start()) {
        std::cerr << "[-] Failed to start Traditional server on port " << port << "\n";
        return 1;
    }

    std::cout << "[+] Traditional Architecture Service active on port " << port << "!\n";
    std::cout << "[*] Press Ctrl+C to terminate.\n";

    while (server.is_running()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}
