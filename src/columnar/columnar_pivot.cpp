#include "columnar/columnar_pivot.hpp"
#include <algorithm>
#include <iostream>

#if defined(_OPENMP)
#include <omp.h>
#endif

namespace riskdb {

size_t ColumnarPivot::pivot(const std::vector<RawOptionRecord>& raw_records, ColumnarTable& out_table) {
    const size_t total_input = raw_records.size();
    if (total_input == 0) {
        out_table.clear();
        return 0;
    }

    out_table.clear();
    out_table.reserve(total_input);

    // Filter non-physical values and push to columnar buffers
    size_t valid_count = 0;
    for (size_t i = 0; i < total_input; ++i) {
        const auto& rec = raw_records[i];
        if (rec.is_valid()) {
            out_table.append(rec);
            ++valid_count;
        }
    }

    return valid_count;
}

std::vector<ColumnarChunk> ColumnarPivot::create_chunks(
    const ColumnarTable& table,
    size_t num_chunks,
    size_t simd_lane_width
) {
    std::vector<ColumnarChunk> chunks;
    const size_t total_records = table.size();
    if (total_records == 0 || num_chunks == 0) {
        return chunks;
    }

    if (num_chunks == 1 || total_records <= simd_lane_width) {
        chunks.emplace_back(0, total_records, &table);
        return chunks;
    }

    chunks.reserve(num_chunks);
    const size_t raw_chunk_size = total_records / num_chunks;
    // Align chunk size to multiple of simd_lane_width (e.g. 16 floats = 64 bytes)
    const size_t aligned_chunk_size = ((raw_chunk_size + simd_lane_width - 1) / simd_lane_width) * simd_lane_width;

    size_t current_idx = 0;
    for (size_t i = 0; i < num_chunks; ++i) {
        if (current_idx >= total_records) break;

        size_t chunk_len = aligned_chunk_size;
        if (i == num_chunks - 1 || current_idx + chunk_len > total_records) {
            chunk_len = total_records - current_idx;
        }

        chunks.emplace_back(current_idx, chunk_len, &table);
        current_idx += chunk_len;
    }

    return chunks;
}

} // namespace riskdb
