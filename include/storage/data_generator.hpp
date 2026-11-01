#pragma once

#include <vector>
#include <random>
#include <string>
#include "common/types.hpp"
#include "columnar/columnar_table.hpp"

namespace riskdb {

struct GeneratorConfig {
    size_t num_rows{1'000'000};        // Default 1M, scalable to 10M (10^7)
    size_t num_underlyings{50};        // Fixed at 50 underlyings
    size_t num_portfolios{100};        // 50 - 500 portfolios
    float call_ratio{0.55f};           // ~55% Calls, ~45% Puts
    float itm_ratio{0.25f};            // 25% In-The-Money
    float atm_ratio{0.35f};            // 35% At-The-Money
    float otm_ratio{0.40f};            // 40% Out-Of-The-Money
    float min_expiry_years{0.019178f}; // ~1 week (7/365)
    float max_expiry_years{2.0f};      // 2 years
    uint32_t random_seed{42};
};

class DataGenerator {
public:
    explicit DataGenerator(const GeneratorConfig& config = GeneratorConfig());

    /**
     * @brief Generates synthetic options portfolio dataset into row-oriented vector.
     */
    std::vector<RawOptionRecord> generate_rows() const;

    /**
     * @brief Generates synthetic dataset directly into cache-aligned ColumnarTable.
     */
    void generate_columnar(ColumnarTable& table) const;

    /**
     * @brief Exports generated records to a CSV file suitable for PostgreSQL COPY ingestion.
     */
    size_t export_to_csv(const std::string& filepath, size_t row_count) const;

private:
    GeneratorConfig config_;
};

} // namespace riskdb
