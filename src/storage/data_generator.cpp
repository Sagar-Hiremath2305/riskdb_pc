#include "storage/data_generator.hpp"
#include <fstream>
#include <iostream>
#include <iomanip>

namespace riskdb {

DataGenerator::DataGenerator(const GeneratorConfig& config)
    : config_(config) {}

std::vector<RawOptionRecord> DataGenerator::generate_rows() const {
    std::vector<RawOptionRecord> rows;
    rows.reserve(config_.num_rows);

    std::mt19937 rng(config_.random_seed);
    std::uniform_real_distribution<float> dist_01(0.0f, 1.0f);
    std::uniform_real_distribution<float> dist_expiry(config_.min_expiry_years, config_.max_expiry_years);
    std::uniform_real_distribution<float> dist_r(0.02f, 0.05f); // 2% to 5% risk free rate
    std::uniform_int_distribution<uint16_t> dist_portfolio(0, static_cast<uint16_t>(config_.num_portfolios - 1));
    std::uniform_int_distribution<uint32_t> dist_underlying(0, static_cast<uint32_t>(config_.num_underlyings - 1));
    std::uniform_real_distribution<float> dist_qty(-200.0f, 500.0f); // mixed long/short positions

    // Base attributes for the 50 underlyings
    std::vector<float> underlying_spots(config_.num_underlyings);
    std::vector<float> underlying_vols(config_.num_underlyings);
    std::uniform_real_distribution<float> dist_spot(30.0f, 450.0f);
    std::uniform_real_distribution<float> dist_vol(0.15f, 0.65f);

    for (size_t u = 0; u < config_.num_underlyings; ++u) {
        underlying_spots[u] = dist_spot(rng);
        underlying_vols[u]  = dist_vol(rng);
    }

    for (size_t i = 0; i < config_.num_rows; ++i) {
        RawOptionRecord rec;
        rec.contract_id = i + 1;
        rec.underlying_id = dist_underlying(rng);
        rec.portfolio_id = dist_portfolio(rng);

        const float spot = underlying_spots[rec.underlying_id];
        const float base_vol = underlying_vols[rec.underlying_id];
        rec.spot_price = spot;

        // Jitter volatility slightly per contract (+-10%)
        rec.volatility = std::max(0.05f, base_vol * (0.90f + 0.20f * dist_01(rng)));
        rec.time_to_expiry = dist_expiry(rng);
        rec.risk_free_rate = dist_r(rng);
        rec.quantity = std::round(dist_qty(rng));
        if (rec.quantity == 0.0f) rec.quantity = 100.0f;

        // ~55% Calls, ~45% Puts
        const bool is_call = (dist_01(rng) < config_.call_ratio);
        rec.option_type = is_call ? 0 : 1;

        // Moneyness: 25% ITM, 35% ATM, 40% OTM
        const float m_roll = dist_01(rng);
        float strike_factor = 1.0f;

        if (m_roll < config_.itm_ratio) {
            // In-The-Money
            if (is_call) {
                // Call ITM: K < S (0.75 to 0.95)
                strike_factor = 0.75f + 0.20f * dist_01(rng);
            } else {
                // Put ITM: K > S (1.05 to 1.25)
                strike_factor = 1.05f + 0.20f * dist_01(rng);
            }
        } else if (m_roll < (config_.itm_ratio + config_.atm_ratio)) {
            // At-The-Money: K ~ S (0.95 to 1.05)
            strike_factor = 0.95f + 0.10f * dist_01(rng);
        } else {
            // Out-Of-The-Money
            if (is_call) {
                // Call OTM: K > S (1.05 to 1.30)
                strike_factor = 1.05f + 0.25f * dist_01(rng);
            } else {
                // Put OTM: K < S (0.70 to 0.95)
                strike_factor = 0.70f + 0.25f * dist_01(rng);
            }
        }

        rec.strike_price = std::max(1.0f, spot * strike_factor);
        rows.push_back(rec);
    }

    return rows;
}

void DataGenerator::generate_columnar(ColumnarTable& table) const {
    const auto rows = generate_rows();
    table.clear();
    table.reserve(rows.size());
    for (const auto& r : rows) {
        table.append(r);
    }
}

size_t DataGenerator::export_to_csv(const std::string& filepath, size_t row_count) const {
    GeneratorConfig temp_cfg = config_;
    temp_cfg.num_rows = row_count;
    DataGenerator temp_gen(temp_cfg);
    const auto rows = temp_gen.generate_rows();

    std::ofstream out(filepath);
    if (!out.is_open()) {
        throw std::runtime_error("Failed to open file for CSV export: " + filepath);
    }

    // Write CSV Header matching PostgreSQL table schema
    out << "contract_id,underlying_id,portfolio_id,option_type,spot_price,strike_price,time_to_expiry,volatility,risk_free_rate,quantity\n";

    out << std::fixed << std::setprecision(4);
    for (const auto& r : rows) {
        out << r.contract_id << ","
            << r.underlying_id << ","
            << r.portfolio_id << ","
            << static_cast<int>(r.option_type) << ","
            << r.spot_price << ","
            << r.strike_price << ","
            << r.time_to_expiry << ","
            << r.volatility << ","
            << r.risk_free_rate << ","
            << r.quantity << "\n";
    }

    return rows.size();
}

} // namespace riskdb
