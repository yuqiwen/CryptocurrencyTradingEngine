#include "timescaledb_reader.h"
#include <libpq-fe.h>
#include <iostream>

TimescaleDBReader::TimescaleDBReader(const std::string& conninfo) {
    conn_ = PQconnectdb(conninfo.c_str());
    if (PQstatus((PGconn*)conn_) != CONNECTION_OK) {
        std::cerr << "Connection to database failed: " << PQerrorMessage((PGconn*)conn_) << std::endl;
        PQfinish((PGconn*)conn_);
        conn_ = nullptr;
    }
}

TimescaleDBReader::~TimescaleDBReader() {
    if (conn_) {
        PQfinish((PGconn*)conn_);
    }
}

std::vector<RawRecord> TimescaleDBReader::read_latest_raw() {
    std::vector<RawRecord> result;
    if (!conn_) return result;
    // 按 (exchange, symbol) 分组，取最新一条
    const char* sql =
        "SELECT DISTINCT ON (exchange, symbol) id, exchange, symbol, last, bid, ask, high, low, volume, timestamp "
        "FROM crypto_raw_prices ORDER BY exchange, symbol, timestamp DESC;";
    PGresult* res = PQexec((PGconn*)conn_, sql);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        PQclear(res);
        return result;
    }
    int rows = PQntuples(res);
    for (int i = 0; i < rows; ++i) {
        RawRecord rec;
        rec.id = std::stoi(PQgetvalue(res, i, 0));
        rec.exchange = PQgetvalue(res, i, 1);
        rec.symbol = PQgetvalue(res, i, 2);
        rec.last = std::stod(PQgetvalue(res, i, 3));
        rec.bid = std::stod(PQgetvalue(res, i, 4));
        rec.ask = std::stod(PQgetvalue(res, i, 5));
        rec.high = std::stod(PQgetvalue(res, i, 6));
        rec.low = std::stod(PQgetvalue(res, i, 7));
        rec.volume = std::stod(PQgetvalue(res, i, 8));
        rec.timestamp = std::stol(PQgetvalue(res, i, 9));
        result.push_back(rec);
    }
    PQclear(res);
    return result;
}

std::vector<PriceStatsRecord> TimescaleDBReader::read_latest_price_stats() {
    std::vector<PriceStatsRecord> result;
    if (!conn_) return result;
    // 按 symbol 分组，取最新一条
    const char* sql =
        "SELECT DISTINCT ON (symbol) id, symbol, highest_price, highest_exchange, lowest_price, lowest_exchange, record_count, earliest_timestamp, latest_timestamp "
        "FROM crypto_price_stats ORDER BY symbol, latest_timestamp DESC;";
    PGresult* res = PQexec((PGconn*)conn_, sql);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        PQclear(res);
        return result;
    }
    int rows = PQntuples(res);
    for (int i = 0; i < rows; ++i) {
        PriceStatsRecord rec;
        rec.id = std::stoi(PQgetvalue(res, i, 0));
        rec.symbol = PQgetvalue(res, i, 1);
        rec.highest_price = std::stod(PQgetvalue(res, i, 2));
        rec.highest_exchange = PQgetvalue(res, i, 3);
        rec.lowest_price = std::stod(PQgetvalue(res, i, 4));
        rec.lowest_exchange = PQgetvalue(res, i, 5);
        rec.record_count = std::stoi(PQgetvalue(res, i, 6));
        rec.earliest_timestamp = std::stol(PQgetvalue(res, i, 7));
        rec.latest_timestamp = std::stol(PQgetvalue(res, i, 8));
        result.push_back(rec);
    }
    PQclear(res);
    return result;
} 