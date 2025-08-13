#pragma once
#include <string>
#include <vector>
#include <memory>

// 前向声明，避免包含 hiredis 头文件
struct redisContext;

// 重用 TimescaleDB 的数据结构
struct RawRecord;
struct PriceStatsRecord;

class RedisWriter {
public:
    RedisWriter(const std::string& host = "127.0.0.1", int port = 6379, const std::string& password = "");
    ~RedisWriter();

    // 连接状态检查
    bool is_connected() const;
    
    // === 写入操作 ===
    // 写入单条 raw 记录
    bool write_raw_record(const RawRecord& record);
    
    // 批量写入 raw 记录
    bool write_raw_records(const std::vector<RawRecord>& records);
    
    // 写入单条 price stats 记录
    bool write_price_stats_record(const PriceStatsRecord& record);
    
    // 批量写入 price stats 记录
    bool write_price_stats_records(const std::vector<PriceStatsRecord>& records);
    
    // === 读取操作 ===
    // 读取单条 raw 记录
    bool read_raw_record(const std::string& exchange, const std::string& symbol, RawRecord& record);
    
    // 读取所有 raw 记录
    std::vector<RawRecord> read_all_raw_records();
    
    // 读取特定交易所的所有记录
    std::vector<RawRecord> read_raw_records_by_exchange(const std::string& exchange);
    
    // 读取单条 price stats 记录
    bool read_price_stats_record(const std::string& symbol, PriceStatsRecord& record);
    
    // 读取所有 price stats 记录
    std::vector<PriceStatsRecord> read_all_price_stats_records();
    
    // === 查询操作 ===
    // 检查 key 是否存在
    bool exists_raw_record(const std::string& exchange, const std::string& symbol);
    bool exists_price_stats_record(const std::string& symbol);
    
    // 获取所有 raw 记录的 keys
    std::vector<std::string> get_all_raw_keys();
    
    // 获取所有 price stats 记录的 keys
    std::vector<std::string> get_all_price_stats_keys();
    
    // 获取 key 的剩余过期时间（秒）
    int get_ttl(const std::string& key);
    
    // === 管理操作 ===
    // 设置过期时间（秒）
    void set_expire_time(int seconds) { expire_time_ = seconds; }
    
    // 清理操作
    bool clear_raw_data();
    bool clear_price_stats_data();

private:
    redisContext* context_;
    int expire_time_;  // 数据过期时间，默认1小时
    
    // 内部辅助方法
    bool connect();
    void disconnect();
    std::string serialize_raw_record(const RawRecord& record);
    std::string serialize_price_stats_record(const PriceStatsRecord& record);
    RawRecord deserialize_raw_record(const std::string& json_str);
    PriceStatsRecord deserialize_price_stats_record(const std::string& json_str);
    std::string get_raw_key(const std::string& exchange, const std::string& symbol);
    std::string get_price_stats_key(const std::string& symbol);
    
    // 连接参数
    std::string host_;
    int port_;
    std::string password_;
};