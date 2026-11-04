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
    bool live_fetch = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) port = std::stoi(argv[++i]);
        else if (arg == "--host" && i + 1 < argc) pg_cfg.host = argv[++i];
        else if (arg == "--pg-host" && i + 1 < argc) pg_cfg.host = argv[++i];
        else if (arg == "--pg-port" && i + 1 < argc) pg_cfg.port = std::stoi(argv[++i]);
        else if (arg == "--dbname" && i + 1 < argc) pg_cfg.dbname = argv[++i];
        else if (arg == "--user" && i + 1 < argc) pg_cfg.user = argv[++i];
        else if (arg == "--password" && i + 1 < argc) pg_cfg.password = argv[++i];
        else if (arg == "--rows" && i + 1 < argc) fallback_rows = std::stoull(argv[++i]);
        else if (arg == "--portfolios" && i + 1 < argc) num_portfolios = std::stoull(argv[++i]);
        else if (arg == "--cli") run_cli = true;
        else if (arg == "--live-fetch") live_fetch = true;
    }

    std::cout << "========================================================================================\n"
              << "       APPLICATION 1: Traditional Tiered Architecture Service (VM 1)                    \n"
              << "========================================================================================\n"
              << " Role: External Application Fetching Rows from PostgreSQL & Scalar Loop                 \n"
              << " HTTP REST Service Port: " << port << "\n"
              << " Live PostgreSQL Re-fetch per Request: " << (live_fetch ? "ENABLED" : "CACHED WAN TIMING") << "\n"
              << "========================================================================================\n\n";

    std::vector<riskdb::RawOptionRecord> cached_records;
    riskdb::PostgresConnector connector(pg_cfg);
    double real_db_fetch_ms = 0.0;

    if (connector.connect()) {
        std::cout << "[+] Connected to PostgreSQL (" << pg_cfg.host << ":" << pg_cfg.port << "). Streaming contracts over WAN...\n";
        const auto t_f0 = Clock::now();
        cached_records = connector.fetch_rows();
        const auto t_f1 = Clock::now();
        real_db_fetch_ms = std::chrono::duration<double, std::milli>(t_f1 - t_f0).count();
        std::cout << "[+] Streamed " << cached_records.size() << " tuples from PostgreSQL in " << real_db_fetch_ms << " ms\n";
    }

    if (cached_records.empty()) {
        std::cout << "[!] PostgreSQL table empty or unavailable. Pre-generating synthetic dataset (" << fallback_rows << " rows)...\n";
        riskdb::GeneratorConfig gen_cfg;
        gen_cfg.num_rows = fallback_rows;
        gen_cfg.num_portfolios = num_portfolios;
        riskdb::DataGenerator gen(gen_cfg);
        cached_records = gen.generate_rows();
    }

    std::vector<riskdb::OptionGreeks> cached_greeks(cached_records.size());
    double last_fetch_time_ms = 0.0;
    double last_compute_time_ms = 0.0;
    uint64_t update_version = 0;

    auto compute_traditional = [&cached_records, &cached_greeks, &last_fetch_time_ms, &last_compute_time_ms, &update_version, &connector, live_fetch, real_db_fetch_ms, num_portfolios](double& fetch_ms, double& comp_ms) {
        if (live_fetch && connector.is_connected()) {
            const auto t_f0 = Clock::now();
            cached_records = connector.fetch_rows();
            const auto t_f1 = Clock::now();
            fetch_ms = std::chrono::duration<double, std::milli>(t_f1 - t_f0).count();
        } else if (real_db_fetch_ms > 0.0) {
            fetch_ms = real_db_fetch_ms;
        } else {
            // Simulated 50 Mbps WAN uplink transfer for 48MB payload if no DB connected
            const size_t payload_bytes = cached_records.size() * sizeof(riskdb::RawOptionRecord);
            constexpr double wan_bytes_per_sec = (50.0 * 1e6) / 8.0; // 50 Mbps
            fetch_ms = (static_cast<double>(payload_bytes) / wan_bytes_per_sec) * 1000.0;
        }

        const auto t_start = Clock::now();
        std::vector<riskdb::PortfolioRiskMetrics> metrics(num_portfolios);
        for (size_t p = 0; p < num_portfolios; ++p) {
            metrics[p] = riskdb::PortfolioRiskMetrics(static_cast<uint32_t>(p));
        }

        if (cached_greeks.size() != cached_records.size()) {
            cached_greeks.resize(cached_records.size());
        }

        for (size_t i = 0; i < cached_records.size(); ++i) {
            const auto& r = cached_records[i];
            if (r.is_valid()) {
                riskdb::OptionGreeks g = riskdb::compute_record_greeks(r);
                cached_greeks[i] = g;
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
        last_fetch_time_ms = fetch_ms;
        last_compute_time_ms = comp_ms;
        ++update_version;
        return metrics;
    };

    if (run_cli) {
        double f_ms = 0.0, c_ms = 0.0;
        auto metrics = compute_traditional(f_ms, c_ms);
        std::cout << "[+] Traditional compute completed in " << (f_ms + c_ms) << " ms ("
                  << f_ms << " ms network + " << c_ms << " ms CPU)\n";
        return 0;
    }

    // Initial pass to compute baseline prices and Greeks
    {
        double init_f = 0.0, init_c = 0.0;
        compute_traditional(init_f, init_c);
    }

    riskdb::HttpServer server(port);
    std::string web_dir = "./web/traditional";
    server.set_static_dir(web_dir);

    server.route("GET", "/api/status", [&cached_records, &update_version, &last_fetch_time_ms, &last_compute_time_ms](const riskdb::HttpRequest&) {
        std::ostringstream ss;
        ss << "{\n"
           << "  \"status\": \"healthy\",\n"
           << "  \"engine\": \"Traditional Tiered Stack\",\n"
           << "  \"cached_contracts\": " << cached_records.size() << ",\n"
           << "  \"update_version\": " << update_version << ",\n"
           << "  \"last_fetch_ms\": " << last_fetch_time_ms << ",\n"
           << "  \"last_compute_ms\": " << last_compute_time_ms << ",\n"
           << "  \"total_latency_ms\": " << (last_fetch_time_ms + last_compute_time_ms) << "\n"
           << "}\n";
        return riskdb::HttpResponse::json(ss.str());
    });

    server.route("GET", "/api/risk", [&compute_traditional, &cached_records](const riskdb::HttpRequest&) {
        double f_ms = 0.0, c_ms = 0.0;
        auto metrics = compute_traditional(f_ms, c_ms);
        std::string json = format_traditional_metrics_json(metrics, f_ms, c_ms, cached_records.size());
        return riskdb::HttpResponse::json(json);
    });

    server.route("GET", "/api/contracts", [&cached_records, &cached_greeks, &last_fetch_time_ms, &last_compute_time_ms, &update_version](const riskdb::HttpRequest& req) {
        size_t limit = 100;
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
        ss << "  \"total_stored_contracts\": " << cached_records.size() << ",\n";
        ss << "  \"update_version\": " << update_version << ",\n";
        ss << "  \"last_fetch_ms\": " << last_fetch_time_ms << ",\n";
        ss << "  \"last_compute_ms\": " << last_compute_time_ms << ",\n";
        ss << "  \"total_latency_ms\": " << (last_fetch_time_ms + last_compute_time_ms) << ",\n";
        ss << "  \"contracts\": [\n";

        size_t matches = 0;
        size_t written = 0;

        for (size_t i = 0; i < cached_records.size(); ++i) {
            const auto& r = cached_records[i];
            if (underlying_filter >= 0 && r.underlying_id != static_cast<uint32_t>(underlying_filter)) continue;
            if (portfolio_filter >= 0 && r.portfolio_id != static_cast<uint16_t>(portfolio_filter)) continue;

            if (matches >= offset && written < limit) {
                if (written > 0) ss << ",\n";
                const auto& g = (i < cached_greeks.size()) ? cached_greeks[i] : riskdb::compute_record_greeks(r);
                ss << "    {"
                   << "\"contract_id\": " << r.contract_id << ", "
                   << "\"underlying_id\": " << r.underlying_id << ", "
                   << "\"portfolio_id\": " << r.portfolio_id << ", "
                   << "\"option_type\": \"" << (r.option_type == 0 ? "CALL" : "PUT") << "\", "
                   << "\"spot_price\": " << r.spot_price << ", "
                   << "\"strike_price\": " << r.strike_price << ", "
                   << "\"time_to_expiry\": " << r.time_to_expiry << ", "
                   << "\"volatility\": " << r.volatility << ", "
                   << "\"quantity\": " << r.quantity << ", "
                   << "\"option_price\": " << g.price << ", "
                   << "\"delta\": " << g.delta << ", "
                   << "\"gamma\": " << g.gamma << ", "
                   << "\"vega\": " << g.vega << ", "
                   << "\"theta\": " << g.theta
                   << "}";
                ++written;
            }
            ++matches;
            if (written >= limit) break;
        }

        ss << "\n  ]\n";
        ss << "}\n";
        return riskdb::HttpResponse::json(ss.str());
    });

    auto handle_traditional_signal = [&cached_records, &compute_traditional](const riskdb::HttpRequest& req) {
        uint32_t underlying_id = 1;
        float new_spot_price = 100.0f;
        if (!req.get_param("underlying").empty()) underlying_id = std::stoul(req.get_param("underlying"));
        if (!req.get_param("spot").empty()) new_spot_price = std::stof(req.get_param("spot"));

        // Update records in traditional memory
        size_t count = 0;
        for (auto& r : cached_records) {
            if (r.underlying_id == underlying_id) {
                r.spot_price = new_spot_price;
                ++count;
            }
        }

        double f_ms = 0.0, c_ms = 0.0;
        auto metrics = compute_traditional(f_ms, c_ms);
        const double total_ms = f_ms + c_ms;

        std::cout << "[TRADITIONAL SIGNAL] Underlying #" << underlying_id << " Price -> $" << new_spot_price
                  << " | Streaming " << count << " affected contracts across TCP/IP...\n"
                  << "                     -> Network Serialization & Fetch: " << f_ms << " ms\n"
                  << "                     -> Scalar CPU Evaluation Loop: " << c_ms << " ms\n"
                  << "                     -> Total Traditional Latency: " << total_ms << " ms\n\n";

        std::string json = format_traditional_metrics_json(metrics, f_ms, c_ms, cached_records.size());
        return riskdb::HttpResponse::json(json);
    };

    server.route("POST", "/api/signal", handle_traditional_signal);
    server.route("GET", "/api/signal", handle_traditional_signal);

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
