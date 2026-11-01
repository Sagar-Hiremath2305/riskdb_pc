#pragma once

#include <vector>
#include "columnar_table.hpp"

namespace riskdb {

class ColumnarPivot {
public:
    /**
     * @brief Pivots an array of row-oriented RawOptionRecord tuples into cache-aligned ColumnarTable.
     * Filters out non-physical records (tau <= 0, K <= 0, S <= 0, sigma <= 0).
     * 
     * @param raw_records Input row-based records from PostgreSQL or synthetic generator.
     * @param out_table Destination ColumnarTable with 64-byte aligned arrays.
     * @return size_t Count of valid records ingested.
     */
    static size_t pivot(const std::vector<RawOptionRecord>& raw_records, ColumnarTable& out_table);

    /**
     * @brief Generates cache-aligned and SIMD-lane aligned chunks for thread dispatching.
     * 
     * @param table Target columnar table
     * @param num_chunks Number of partitions (typically matching worker thread count)
     * @param simd_lane_width Element alignment for chunk boundaries (default 16 elements = 64 bytes)
     * @return std::vector<ColumnarChunk> Array of chunks distributed across worker threads
     */
    static std::vector<ColumnarChunk> create_chunks(
        const ColumnarTable& table,
        size_t num_chunks,
        size_t simd_lane_width = 16
    );
};

} // namespace riskdb
