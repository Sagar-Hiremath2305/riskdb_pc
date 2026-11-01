#include <iostream>
#include <cassert>
#include <vector>
#include "columnar/columnar_table.hpp"
#include "columnar/columnar_pivot.hpp"
#include "common/types.hpp"

int main() {
    std::cout << "[*] Running Columnar Pivot & Chunk Partitioning Test...\n";

    std::vector<riskdb::RawOptionRecord> records;
    for (size_t i = 0; i < 1000; ++i) {
        riskdb::RawOptionRecord r;
        r.contract_id = i + 1;
        r.underlying_id = static_cast<uint32_t>(i % 50);
        r.portfolio_id = static_cast<uint16_t>(i % 10);
        r.option_type = (i % 2 == 0) ? 0 : 1;
        r.spot_price = 100.0f;
        r.strike_price = 105.0f;
        r.time_to_expiry = (i == 50) ? -1.0f : 0.5f; // i=50 has invalid negative expiry
        r.volatility = (i == 60) ? -0.2f : 0.25f;    // i=60 has invalid negative volatility
        r.risk_free_rate = 0.04f;
        r.quantity = 10.0f;
        records.push_back(r);
    }

    riskdb::ColumnarTable table;
    size_t valid = riskdb::ColumnarPivot::pivot(records, table);

    // 2 invalid records should be rejected
    assert(valid == 998);
    assert(table.size() == 998);

    // Test Chunk Creation across 8 threads
    auto chunks = riskdb::ColumnarPivot::create_chunks(table, 8, 16);
    assert(!chunks.empty());

    size_t total_chunked = 0;
    for (const auto& ch : chunks) {
        assert(ch.table == &table);
        total_chunked += ch.length;
    }
    assert(total_chunked == 998);

    std::cout << "[+] Columnar Pivot & Chunk Partitioning PASSED.\n";
    return 0;
}
