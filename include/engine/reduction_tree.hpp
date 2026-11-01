#pragma once

#include <vector>
#include <cmath>
#include "local_accumulator.hpp"

#if defined(_OPENMP)
#include <omp.h>
#endif

namespace riskdb {

class ReductionTree {
public:
    /**
     * @brief Executes an O(log T) parallel logarithmic reduction tree merging T thread accumulators
     * into accumulator[0].
     * 
     * @param accumulators Array of T thread-local accumulators
     * @param num_threads Number of active thread accumulators (T)
     */
    static void reduce(std::vector<ThreadLocalAccumulator>& accumulators, size_t num_threads) {
        if (num_threads <= 1 || accumulators.empty()) {
            return;
        }

        // Tree reduction takes ceil(log2(T)) parallel stages
        for (size_t stride = 1; stride < num_threads; stride *= 2) {
            #pragma omp parallel for schedule(static)
            for (size_t i = 0; i < num_threads; i += 2 * stride) {
                if (i + stride < num_threads) {
                    accumulators[i].merge_from(accumulators[i + stride]);
                }
            }
        }
    }
};

} // namespace riskdb
