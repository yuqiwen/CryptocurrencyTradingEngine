#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <thread>
#include <chrono>

// 你项目中核心模块的头文件
#include "redis_writer.h"
#include "data_sync_service.h"
#include "market_making_strategy.h"
#include "arbitrage_strategy.h"


// Engine 状态枚举
enum class TradingMode {
    MARKET_MAKING,
    ARBITRAGE,
    MIXED
};

enum class EngineStatus {
    STOPPED,
    STARTING,
    RUNNING,
    STOPPING,
    ERROR
};

// 客户请求结构
struct ClientRequest {
    std::string client_id;
    std::string symbol;
    std::string exchange;
    double max_amount;
    double target_profit;
    TradingMode mode;

    // 新增止盈 / 止损百分比（默认 10% / 5%）
    double take_profit_ratio = 0.10;  // 止盈（如 0.10 表示 +10%）
    double stop_loss_ratio = 0.05;    // 止损（如 0.05 表示 -5%）
};

// 会话结构
struct TradingSession {
    std::string session_id;
    ClientRequest request;
    EngineStatus status;

    std::unique_ptr<MarketMakingStrategy> market_making_strategy;
    std::unique_ptr<ArbitrageStrategy> arbitrage_strategy;

    double total_profit;
    int executed_trades;
    std::vector<std::string> log;

    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point last_update;
};

// 引擎统计信息结构
struct EngineStats {
    int total_sessions_created;
    int active_sessions;
    int total_trades_executed;
    double total_profit_generated;

    std::chrono::system_clock::time_point engine_start_time;
    std::chrono::system_clock::time_point last_update_time;
};

class TradingEngineManager {
public:
    TradingEngineManager(const std::string& db_conninfo,
                         const std::string& redis_host,
                         int redis_port,
                         const std::string& redis_password);
    ~TradingEngineManager();

    bool initialize();
    void shutdown();

    void start_engine();
    void stop_engine();

    bool is_healthy() const;

    std::string create_trading_session(const ClientRequest& request);
    bool start_trading_session(const std::string& session_id);
    bool stop_trading_session(const std::string& session_id);
    bool remove_trading_session(const std::string& session_id);

    std::vector<std::string> get_active_sessions() const;
    TradingSession* get_session(const std::string& session_id);

private:
    // 主循环
    void trading_loop();
    void execute_trading_session(TradingSession* session);
    void execute_arbitrage_session(TradingSession* session);
    void execute_market_making_session(TradingSession* session);

    // 会话管理
    bool validate_client_request(const ClientRequest& request) const;
    void log_session_activity(const std::string& session_id, const std::string& message) const;
    std::string generate_session_id();
    void update_session_stats(TradingSession* session, double profit, int trades);
    void cleanup_expired_sessions();

    // 状态与配置
    void reset_stats();

private:
    EngineStatus engine_status_;
    bool should_run_;
    int trading_interval_ms_;
    int max_sessions_;

    std::shared_ptr<RedisWriter> redis_client_;
    std::unique_ptr<DataSyncService> data_sync_service_;
    std::unordered_map<std::string, std::unique_ptr<TradingSession>> trading_sessions_;

    EngineStats stats_;
    std::unique_ptr<std::thread> engine_thread_;
};
