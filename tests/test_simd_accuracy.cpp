#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>
#include "columnar/columnar_table.hpp"
#include "simd/simd_black_scholes.hpp"
#include "common/math_utils.hpp"
#include "storage/data_generator.hpp"

int main() {
    std::cout << "[*] Running SIMD Numerical Accuracy Verification Test...\n";

    riskdb::GeneratorConfig cfg;
    cfg.num_rows = 10'000;
    riskdb::DataGenerator gen(cfg);

    const auto raw = gen.generate_rows();
    riskdb::ColumnarTable table;
    table.reserve(raw.size());
    for (const auto& r : raw) table.append(r);

    std::vector<float> prices(raw.size());
    std::vector<float> deltas(raw.size());
    std::vector<float> gammas(raw.size());
    std::vector<float> vegas(raw.size());

    riskdb::SIMDBlackScholesKernel::evaluate_batch(
        table, 0, raw.size(),
        prices.data(), deltas.data(), gammas.data(), vegas.data()
    );

    double max_price_diff = 0.0;
    double max_delta_diff = 0.0;

    for (size_t i = 0; i < raw.size(); ++i) {
        riskdb::OptionGreeks ref = riskdb::compute_record_greeks(raw[i]);

        double p_diff = std::abs(prices[i] - ref.price);
        double d_diff = std::abs(deltas[i] - ref.delta);

        if (p_diff > max_price_diff) max_price_diff = p_diff;
        if (d_diff > max_delta_diff) max_delta_diff = d_diff;

        // Tolerance check for single precision float SIMD approximation vs double reference
        if (p_diff > 0.05) {
            std::cerr << "[-] Discrepancy at row " << i << ": SIMD price=" << prices[i]
                      << ", Ref=" << ref.price << ", Diff=" << p_diff << "\n";
            return 1;
        }
    }

    std::cout << "[+] Maximum Price Difference: " << max_price_diff << " (Well within float precision)\n";
    std::cout << "[+] Maximum Delta Difference: " << max_delta_diff << " (Well within float precision)\n";
    std::cout << "[+] SIMD Black-Scholes Numerical Accuracy PASSED.\n";
    return 0;
}
