#pragma once
#include "redis_writer.h"
#include "timescaledb_reader.h"
#include "strategy_result.h"

#include <string>
#include <memory>


/**
 * 套利策略
 * 功能：
 * 1. 从 Redis 读取市场数据
 * 2. 计算价差，判断是否存在套利机会
 * 3. 考虑手续费后计算净利润
 * 4. 显示套利机会和建议操作
 */
class ArbitrageStrategy {
public:
    ArbitrageStrategy(std::shared_ptr<RedisWriter> redis_client,
                      const std::string& symbol);
    
    // 核心方法
    StrategyResult run_once();
    
    // 策略配置
    void set_min_profit_bps(double min_profit_bps);
    void set_max_trade_size(double max_size);
    
    // 状态查询
    bool is_healthy() const;
    void print_status() const;

    // 获取某个交易所的手续费（bps）
    static double get_exchange_fee(const std::string& exchange);

private:
    // 单个交易所的数据结构（备用，暂未删除）
    struct ExchangeData {
        std::string exchange;
        double bid;
        double ask;
        double last;
        bool is_valid;

        double mid_price() const { return (bid + ask) / 2.0; }
        double spread_bps() const { return (ask - bid) / mid_price() * 10000; }
    };

    // 套利机会结构
    struct ArbitrageOpportunity {
        std::string buy_exchange;
        std::string sell_exchange;
        double buy_price;
        double sell_price;
        double gross_profit_bps;
        double net_profit_bps;
        double max_quantity;
        bool is_profitable;
        std::string reason;
        
    };

    // 内部方法
    ArbitrageOpportunity analyze_price_stats_arbitrage(const PriceStatsRecord& stats);
    double calculate_net_profit_bps(double buy_price, double sell_price, 
                                    const std::string& buy_exchange, 
                                    const std::string& sell_exchange);
    // void print_opportunity(const ArbitrageOpportunity& opportunity);
    std::string get_redis_key(const std::string& exchange) const;

    // 成员变量
    std::shared_ptr<RedisWriter> redis_client_;
    std::string symbol_;
    
    // 策略参数
    double min_profit_bps_;
    double max_trade_size_;

};
