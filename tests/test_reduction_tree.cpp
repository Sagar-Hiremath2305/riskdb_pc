#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>
#include "engine/local_accumulator.hpp"
#include "engine/reduction_tree.hpp"

int main() {
    std::cout << "[*] Running Logarithmic Reduction Tree Verification Test...\n";

    constexpr size_t NUM_THREADS = 16;
    constexpr size_t NUM_PORTFOLIOS = 100;

    std::vector<riskdb::ThreadLocalAccumulator> accumulators(NUM_THREADS);
    for (size_t t = 0; t < NUM_THREADS; ++t) {
        accumulators[t].init(NUM_PORTFOLIOS);
        for (size_t p = 0; p < NUM_PORTFOLIOS; ++p) {
            accumulators[t].deltas_data()[p] = static_cast<double>((t + 1) * 10 + p);
            accumulators[t].values_data()[p] = static_cast<double>((t + 1) * 100 + p);
            accumulators[t].counts_data()[p] = 1;
        }
    }

    // Compute expected sequential sums
    std::vector<double> expected_deltas(NUM_PORTFOLIOS, 0.0);
    std::vector<double> expected_values(NUM_PORTFOLIOS, 0.0);
    std::vector<uint64_t> expected_counts(NUM_PORTFOLIOS, 0);

    for (size_t t = 0; t < NUM_THREADS; ++t) {
        for (size_t p = 0; p < NUM_PORTFOLIOS; ++p) {
            expected_deltas[p] += static_cast<double>((t + 1) * 10 + p);
            expected_values[p] += static_cast<double>((t + 1) * 100 + p);
            expected_counts[p] += 1;
        }
    }

    // Execute Logarithmic Parallel Reduction
    riskdb::ReductionTree::reduce(accumulators, NUM_THREADS);

    // Verify accumulator[0] matches expected
    const auto& res = accumulators[0];
    for (size_t p = 0; p < NUM_PORTFOLIOS; ++p) {
        assert(std::abs(res.deltas_data()[p] - expected_deltas[p]) < 1e-6);
        assert(std::abs(res.values_data()[p] - expected_values[p]) < 1e-6);
        assert(res.counts_data()[p] == expected_counts[p]);
    }

    std::cout << "[+] Logarithmic O(log T) Reduction Tree Correctness PASSED.\n";
    return 0;
}
