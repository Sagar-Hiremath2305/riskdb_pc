#include "storage/postgres_connector.hpp"
#include <iostream>
#include <sstream>
#include <cstring>

#if __has_include(<libpq-fe.h>)
#include <libpq-fe.h>
#define RISKDB_HAS_LIBPQ 1
#else
#define RISKDB_HAS_LIBPQ 0
#endif

namespace riskdb {

PostgresConnector::PostgresConnector(const PostgresConfig& config)
    : config_(config) {}

PostgresConnector::~PostgresConnector() {
    disconnect();
}

bool PostgresConnector::connect() {
#if RISKDB_HAS_LIBPQ
    disconnect();
    const std::string conninfo = config_.to_conninfo();
    PGconn* pg_conn = PQconnectdb(conninfo.c_str());

    if (PQstatus(pg_conn) != CONNECTION_OK) {
        last_error_ = PQerrorMessage(pg_conn);
        PQfinish(pg_conn);
        conn_ = nullptr;
        return false;
    }

    conn_ = static_cast<void*>(pg_conn);
    return true;
#else
    last_error_ = "libpq is not available in build environment";
    return false;
#endif
}

void PostgresConnector::disconnect() noexcept {
#if RISKDB_HAS_LIBPQ
    if (conn_) {
        PQfinish(static_cast<PGconn*>(conn_));
        conn_ = nullptr;
    }
#endif
}

bool PostgresConnector::is_connected() const noexcept {
#if RISKDB_HAS_LIBPQ
    return (conn_ != nullptr) && (PQstatus(static_cast<PGconn*>(conn_)) == CONNECTION_OK);
#else
    return false;
#endif
}

bool PostgresConnector::init_schema() {
#if RISKDB_HAS_LIBPQ
    if (!is_connected() && !connect()) return false;
    PGconn* pg_conn = static_cast<PGconn*>(conn_);

    const char* ddl =
        "CREATE TABLE IF NOT EXISTS contracts ("
        "  contract_id BIGINT PRIMARY KEY,"
        "  underlying_id INT NOT NULL,"
        "  portfolio_id INT NOT NULL,"
        "  option_type SMALLINT NOT NULL,"
        "  spot_price REAL NOT NULL,"
        "  strike_price REAL NOT NULL,"
        "  time_to_expiry REAL NOT NULL,"
        "  volatility REAL NOT NULL,"
        "  risk_free_rate REAL NOT NULL,"
        "  quantity REAL NOT NULL"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_contracts_portfolio ON contracts(portfolio_id);";

    PGresult* res = PQexec(pg_conn, ddl);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        last_error_ = PQerrorMessage(pg_conn);
        PQclear(res);
        return false;
    }
    PQclear(res);
    return true;
#else
    last_error_ = "libpq disabled";
    return false;
#endif
}

size_t PostgresConnector::fetch_to_columnar(ColumnarTable& table, const std::string& query) {
#if RISKDB_HAS_LIBPQ
    if (!is_connected() && !connect()) return 0;
    PGconn* pg_conn = static_cast<PGconn*>(conn_);

    // Use single-row mode or cursor for large datasets if needed, or buffered query
    PGresult* res = PQexecParams(pg_conn, query.c_str(), 0, nullptr, nullptr, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        last_error_ = PQerrorMessage(pg_conn);
        PQclear(res);
        return 0;
    }

    const int n_rows = PQntuples(res);
    table.clear();
    table.reserve(n_rows);

    for (int r = 0; r < n_rows; ++r) {
        RawOptionRecord rec;
        rec.contract_id    = std::stoull(PQgetvalue(res, r, 0));
        rec.underlying_id  = static_cast<uint32_t>(std::stoul(PQgetvalue(res, r, 1)));
        rec.portfolio_id   = static_cast<uint16_t>(std::stoul(PQgetvalue(res, r, 2)));
        rec.option_type    = static_cast<uint8_t>(std::stoi(PQgetvalue(res, r, 3)));
        rec.spot_price     = std::stof(PQgetvalue(res, r, 4));
        rec.strike_price   = std::stof(PQgetvalue(res, r, 5));
        rec.time_to_expiry = std::stof(PQgetvalue(res, r, 6));
        rec.volatility     = std::stof(PQgetvalue(res, r, 7));
        rec.risk_free_rate = std::stof(PQgetvalue(res, r, 8));
        rec.quantity       = std::stof(PQgetvalue(res, r, 9));

        if (rec.is_valid()) {
            table.append(rec);
        }
    }

    PQclear(res);
    return table.size();
#else
    last_error_ = "libpq disabled";
    return 0;
#endif
}

std::vector<RawOptionRecord> PostgresConnector::fetch_rows(const std::string& query) {
    std::vector<RawOptionRecord> rows;
#if RISKDB_HAS_LIBPQ
    if (!is_connected() && !connect()) return rows;
    PGconn* pg_conn = static_cast<PGconn*>(conn_);

    PGresult* res = PQexec(pg_conn, query.c_str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        last_error_ = PQerrorMessage(pg_conn);
        PQclear(res);
        return rows;
    }

    const int n_rows = PQntuples(res);
    rows.reserve(n_rows);

    for (int r = 0; r < n_rows; ++r) {
        RawOptionRecord rec;
        rec.contract_id    = std::stoull(PQgetvalue(res, r, 0));
        rec.underlying_id  = static_cast<uint32_t>(std::stoul(PQgetvalue(res, r, 1)));
        rec.portfolio_id   = static_cast<uint16_t>(std::stoul(PQgetvalue(res, r, 2)));
        rec.option_type    = static_cast<uint8_t>(std::stoi(PQgetvalue(res, r, 3)));
        rec.spot_price     = std::stof(PQgetvalue(res, r, 4));
        rec.strike_price   = std::stof(PQgetvalue(res, r, 5));
        rec.time_to_expiry = std::stof(PQgetvalue(res, r, 6));
        rec.volatility     = std::stof(PQgetvalue(res, r, 7));
        rec.risk_free_rate = std::stof(PQgetvalue(res, r, 8));
        rec.quantity       = std::stof(PQgetvalue(res, r, 9));
        rows.push_back(rec);
    }

    PQclear(res);
#else
    last_error_ = "libpq disabled";
#endif
    return rows;
}

size_t PostgresConnector::bulk_copy_insert(const std::vector<RawOptionRecord>& records) {
#if RISKDB_HAS_LIBPQ
    if (!is_connected() && !connect()) return 0;
    PGconn* pg_conn = static_cast<PGconn*>(conn_);

    const char* copy_sql = "COPY contracts FROM STDIN WITH (FORMAT csv)";
    PGresult* res = PQexec(pg_conn, copy_sql);
    if (PQresultStatus(res) != PGRES_COPY_IN) {
        last_error_ = PQerrorMessage(pg_conn);
        PQclear(res);
        return 0;
    }
    PQclear(res);

    std::string buffer;
    buffer.reserve(1024 * 1024); // 1 MB chunk

    size_t count = 0;
    for (const auto& r : records) {
        std::ostringstream ss;
        ss << r.contract_id << ","
           << r.underlying_id << ","
           << r.portfolio_id << ","
           << static_cast<int>(r.option_type) << ","
           << r.spot_price << ","
           << r.strike_price << ","
           << r.time_to_expiry << ","
           << r.volatility << ","
           << r.risk_free_rate << ","
           << r.quantity << "\n";

        buffer += ss.str();
        if (buffer.size() >= 512 * 1024) {
            if (PQputCopyData(pg_conn, buffer.data(), static_cast<int>(buffer.size())) != 1) {
                last_error_ = PQerrorMessage(pg_conn);
                PQputCopyEnd(pg_conn, "error sending copy data");
                return count;
            }
            buffer.clear();
        }
        ++count;
    }

    if (!buffer.empty()) {
        PQputCopyData(pg_conn, buffer.data(), static_cast<int>(buffer.size()));
    }

    if (PQputCopyEnd(pg_conn, nullptr) != 1) {
        last_error_ = PQerrorMessage(pg_conn);
        return 0;
    }

    res = PQgetResult(pg_conn);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        last_error_ = PQerrorMessage(pg_conn);
        PQclear(res);
        return 0;
    }
    PQclear(res);

    return count;
#else
    last_error_ = "libpq disabled";
    return 0;
#endif
}

} // namespace riskdb
