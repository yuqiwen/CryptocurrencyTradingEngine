#pragma once
#include "redis_writer.h"
#include "timescaledb_reader.h"
#include "strategy_result.h"

#include <string>
#include <memory>



/**
 * 做市策略
 * 功能：
 * 1. 从 Redis 读取市场数据
 * 2. 计算公允价格和买卖价差
 * 3. 显示理论报价
 */
class MarketMakingStrategy {
public:
    MarketMakingStrategy(std::shared_ptr<RedisWriter> redis_client,
                     const std::string& symbol,
                     const std::string& exchange);
    
    // 核心方法
    StrategyResult run_once();
    
    // 配置方法
    void set_spread_bps(double spread_bps);
    void set_order_size(double size);
    
    // 状态查询
    bool is_healthy() const;
    void print_status() const;

private:
    // 市场数据结构
    struct MarketData {
        double bid;
        double ask;
        double last;
        bool is_valid;
        
        double mid_price() const { return (bid + ask) / 2.0; }
        double spread_bps() const { return (ask - bid) / mid_price() * 10000; }
    };
    
    // 内部方法
    MarketData get_market_data();
    void calculate_quotes(double fair_value, double& bid_price, double& ask_price);
    // void print_quotes(double bid_price, double ask_price, const MarketData& market_data);
    std::string get_redis_key() const;
    
    // 成员变量
    std::shared_ptr<RedisWriter> redis_client_;
    std::string symbol_;
    std::string exchange_;
    double spread_bps_;
    double order_size_;

};