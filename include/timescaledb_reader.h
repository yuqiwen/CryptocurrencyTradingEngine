#pragma once
#include <string>
#include <vector>

struct RawRecord {
    int id;
    std::string exchange;
    std::string symbol;
    double last;
    double bid;
    double ask;
    double high;
    double low;
    double volume;
    long timestamp;
};

struct PriceStatsRecord {
    int id;
    std::string symbol;
    double highest_price;
    std::string highest_exchange;
    double lowest_price;
    std::string lowest_exchange;
    int record_count;
    long earliest_timestamp;
    long latest_timestamp;
};

class TimescaleDBReader {
public:
    TimescaleDBReader(const std::string& conninfo);
    ~TimescaleDBReader();

    // 读取每个交易所、每个币种的最新 raw 记录
    std::vector<RawRecord> read_latest_raw();
    // 读取每个币种的最新 price 统计记录
    std::vector<PriceStatsRecord> read_latest_price_stats();

private:
    void* conn_; // PGconn*
}; 