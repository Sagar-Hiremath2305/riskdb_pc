#pragma once

#include <string>
#include <vector>
#include <memory>
#include "columnar/columnar_table.hpp"
#include "common/types.hpp"

namespace riskdb {

struct PostgresConfig {
    std::string host{"localhost"};
    int port{5432};
    std::string dbname{"riskdb"};
    std::string user{"postgres"};
    std::string password{"postgres"};

    std::string to_conninfo() const {
        return "host=" + host +
               " port=" + std::to_string(port) +
               " dbname=" + dbname +
               " user=" + user +
               " password=" + password;
    }
};

class PostgresConnector {
public:
    explicit PostgresConnector(const PostgresConfig& config = PostgresConfig());
    ~PostgresConnector();

    bool connect();
    void disconnect() noexcept;
    bool is_connected() const noexcept;

    /**
     * @brief Creates the contracts schema and indexes on PostgreSQL.
     */
    bool init_schema();

    /**
     * @brief Ingests PostgreSQL contracts table into in-memory ColumnarTable.
     */
    size_t fetch_to_columnar(ColumnarTable& table, const std::string& query = "SELECT contract_id, underlying_id, portfolio_id, option_type, spot_price, strike_price, time_to_expiry, volatility, risk_free_rate, quantity FROM contracts");

    /**
     * @brief Fetches raw row records from PostgreSQL (Traditional Architecture pipeline).
     */
    std::vector<RawOptionRecord> fetch_rows(const std::string& query = "SELECT contract_id, underlying_id, portfolio_id, option_type, spot_price, strike_price, time_to_expiry, volatility, risk_free_rate, quantity FROM contracts");

    /**
     * @brief Bulk inserts raw rows into PostgreSQL using high-throughput COPY command.
     */
    size_t bulk_copy_insert(const std::vector<RawOptionRecord>& records);

    std::string last_error() const { return last_error_; }

private:
    PostgresConfig config_;
    void* conn_{nullptr}; // PGconn pointer
    std::string last_error_;
};

} // namespace riskdb
