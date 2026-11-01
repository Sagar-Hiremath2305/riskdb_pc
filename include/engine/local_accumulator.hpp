#pragma once

#include <vector>
#include <cstdint>
#include <cstring>
#include "common/alignment.hpp"
#include "common/types.hpp"

namespace riskdb {

/**
 * @brief Lock-free, cache-line aligned thread-local accumulator for portfolio risk metrics.
 * 
 * Each worker thread operates strictly within its own private accumulator.
 * Eliminates mutex locks, atomic contention, and inter-core cache-coherency ping-pong.
 */
class alignas(CACHE_LINE_SIZE) ThreadLocalAccumulator {
public:
    ThreadLocalAccumulator() = default;
    explicit ThreadLocalAccumulator(size_t num_portfolios) {
        init(num_portfolios);
    }

    void init(size_t num_portfolios) {
        num_portfolios_ = num_portfolios;
        deltas_.assign(num_portfolios, 0.0);
        values_.assign(num_portfolios, 0.0);
        gammas_.assign(num_portfolios, 0.0);
        vegas_.assign(num_portfolios, 0.0);
        counts_.assign(num_portfolios, 0);
    }

    void reset() noexcept {
        std::fill(deltas_.begin(), deltas_.end(), 0.0);
        std::fill(values_.begin(), values_.end(), 0.0);
        std::fill(gammas_.begin(), gammas_.end(), 0.0);
        std::fill(vegas_.begin(), vegas_.end(), 0.0);
        std::fill(counts_.begin(), counts_.end(), 0);
    }

    size_t num_portfolios() const noexcept { return num_portfolios_; }

    double* deltas_data() noexcept { return deltas_.data(); }
    double* values_data() noexcept { return values_.data(); }
    double* gammas_data() noexcept { return gammas_.data(); }
    double* vegas_data() noexcept { return vegas_.data(); }
    uint64_t* counts_data() noexcept { return counts_.data(); }

    const double* deltas_data() const noexcept { return deltas_.data(); }
    const double* values_data() const noexcept { return values_.data(); }
    const double* gammas_data() const noexcept { return gammas_.data(); }
    const double* vegas_data() const noexcept { return vegas_.data(); }
    const uint64_t* counts_data() const noexcept { return counts_.data(); }

    /**
     * @brief In-place merge of another accumulator into this one.
     */
    void merge_from(const ThreadLocalAccumulator& rhs) noexcept {
        const size_t n = num_portfolios_;
        const double* const __restrict r_del = rhs.deltas_.data();
        const double* const __restrict r_val = rhs.values_.data();
        const double* const __restrict r_gam = rhs.gammas_.data();
        const double* const __restrict r_veg = rhs.vegas_.data();
        const uint64_t* const __restrict r_cnt = rhs.counts_.data();

        double* const __restrict del = deltas_.data();
        double* const __restrict val = values_.data();
        double* const __restrict gam = gammas_.data();
        double* const __restrict veg = vegas_.data();
        uint64_t* const __restrict cnt = counts_.data();

        #pragma omp simd
        for (size_t p = 0; p < n; ++p) {
            del[p] += r_del[p];
            val[p] += r_val[p];
            gam[p] += r_gam[p];
            veg[p] += r_veg[p];
            cnt[p] += r_cnt[p];
        }
    }

    /**
     * @brief Exports accumulated results to standard PortfolioRiskMetrics vector.
     */
    std::vector<PortfolioRiskMetrics> export_metrics() const {
        std::vector<PortfolioRiskMetrics> results;
        results.reserve(num_portfolios_);
        for (size_t p = 0; p < num_portfolios_; ++p) {
            if (counts_[p] > 0) {
                PortfolioRiskMetrics m(static_cast<uint32_t>(p));
                m.total_delta = deltas_[p];
                m.total_value = values_[p];
                m.total_gamma = gammas_[p];
                m.total_vega  = vegas_[p];
                m.contract_count = counts_[p];
                results.push_back(m);
            }
        }
        return results;
    }

private:
    size_t num_portfolios_{0};
    std::vector<double, AlignedAllocator<double>> deltas_;
    std::vector<double, AlignedAllocator<double>> values_;
    std::vector<double, AlignedAllocator<double>> gammas_;
    std::vector<double, AlignedAllocator<double>> vegas_;
    std::vector<uint64_t, AlignedAllocator<uint64_t>> counts_;
};

} // namespace riskdb
