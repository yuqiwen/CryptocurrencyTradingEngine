#include "market_making_strategy.h"
#include "trading_engine_manager.h"
#include <iostream>
#include <iomanip>
#include <cmath>

MarketMakingStrategy::MarketMakingStrategy(std::shared_ptr<RedisWriter> redis_client,
                                           const std::string& symbol,
                                           const std::string& exchange)
    : redis_client_(redis_client), symbol_(symbol), exchange_(exchange){
    
    // 根据币种设置默认参数
    if (symbol == "BTC/USDT") {
        spread_bps_ = 5.0;
        order_size_ = 0.001;
    } else if (symbol == "ETH/USDT") {
        spread_bps_ = 6.0;
        order_size_ = 0.01;
    } else if (symbol == "XRP/USDT") {
        spread_bps_ = 8.0;
        order_size_ = 10.0;
    } else if (symbol == "SOL/USDT") {
        spread_bps_ = 10.0;
        order_size_ = 0.1;
    } else {
        // 默认配置
        spread_bps_ = 15.0;
        order_size_ = 0.01;
    }
    
    std::cout << "MarketMakingStrategy created for " << exchange_ << ":" << symbol_ << std::endl;
    std::cout << "Default spread: " << spread_bps_ << " bps, order size: " << order_size_ << std::endl;
}

StrategyResult MarketMakingStrategy::run_once() {
    StrategyResult result;

    std::ostringstream header;
    header << "\n=== Market Making Run (" << exchange_ << " : " << symbol_ << ") ===";
    std::cout << header.str() << std::endl;
    result.logs.push_back(header.str());

    // 1. 获取市场数据
    auto market_data = get_market_data();
    if (!market_data.is_valid) {
        std::string err = "No valid market data available";
        std::cout << err << std::endl;
        result.logs.push_back(err);
        return result;
    }

    std::ostringstream info;
    info << "Market Data: Bid=" << market_data.bid
         << ", Ask=" << market_data.ask
         << ", Last=" << market_data.last
         << ", Spread=" << std::fixed << std::setprecision(2) << market_data.spread_bps() << "bps";
    std::cout << info.str() << std::endl;
    result.logs.push_back(info.str());

    // 2. 计算公允价格
    double fair_value = market_data.mid_price();
    std::ostringstream fair;
    fair << "Fair Value: " << fair_value;
    std::cout << fair.str() << std::endl;
    result.logs.push_back(fair.str());

    // 3. 计算报价
    double bid_price, ask_price;
    calculate_quotes(fair_value, bid_price, ask_price);

    // 4. 显示报价
    std::ostringstream quote;
    quote << "\nMarket Making Quotes:\n"
          << "  Current Market: " << market_data.bid << " / " << market_data.ask << "\n"
          << "  Our Quotes:    " << bid_price << " / " << ask_price << "\n"
          << "  Our Spread:    "
          << std::fixed << std::setprecision(2)
          << (ask_price - bid_price) / ((bid_price + ask_price) / 2.0) * 10000 << "bps\n"
          << "  Order Size:    " << order_size_ << " " << symbol_.substr(0, 3);
    std::cout << quote.str() << std::endl;
    result.logs.push_back(quote.str());

    std::ostringstream mock_orders;
    mock_orders << "Would place orders:\n"
                << "  BUY  " << order_size_ << " @ " << bid_price << "\n"
                << "  SELL " << order_size_ << " @ " << ask_price;
    std::cout << mock_orders.str() << std::endl;
    result.logs.push_back(mock_orders.str());

    result.trades = 2;
    result.profit = 0.0;  // 暂时没有盈利估算，如有可加真实模型

    return result;
}


MarketMakingStrategy::MarketData MarketMakingStrategy::get_market_data() {
    MarketData data;
    data.is_valid = false;
    
    try {
        std::string redis_key = get_redis_key();
        std::cout << "Reading from Redis key: " << redis_key << std::endl;
        
        RawRecord record;
        bool success = redis_client_->read_raw_record(exchange_, symbol_, record);
        
        if (success) {
            data.bid = record.bid;
            data.ask = record.ask;
            data.last = record.last;
            data.is_valid = true;
            
            std::cout << "Successfully read market data from Redis" << std::endl;
            std::cout << "   Exchange: " << record.exchange 
                      << ", Symbol: " << record.symbol 
                      << ", Timestamp: " << record.timestamp << std::endl;
        } else {
            std::cout << "Failed to read market data from Redis" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cout << "Error reading market data: " << e.what() << std::endl;
    }
    
    return data;
}

void MarketMakingStrategy::calculate_quotes(double fair_value, double& bid_price, double& ask_price) {
    // 计算半价差
    double half_spread = fair_value * spread_bps_ / 10000.0 / 2.0;
    
    bid_price = fair_value - half_spread;
    ask_price = fair_value + half_spread;
    
    // 价格对齐到最小价格单位
    bid_price = std::floor(bid_price * 100) / 100.0;
    ask_price = std::ceil(ask_price * 100) / 100.0;
}

// void MarketMakingStrategy::print_quotes(double bid_price, double ask_price, const MarketData& market_data) {
//     std::ostringstream quote;
//     quote << "\nMarket Making Quotes:\n"
//         << "  Current Market: " << market_data.bid << " / " << market_data.ask << "\n"
//         << "  Our Quotes:    " << bid_price << " / " << ask_price << "\n"
//         << "  Our Spread:    "
//         << std::fixed << std::setprecision(2)
//         << (ask_price - bid_price) / ((bid_price + ask_price) / 2.0) * 10000 << "bps\n"
//         << "  Order Size:    " << order_size_ << " " << symbol_.substr(0, 3) << "\n";

//     std::cout << quote.str() << std::endl;
//     if (session_) session_->log.push_back(quote.str());
    
//    std::ostringstream mock_orders;
//     mock_orders << "Would place orders:\n"
//                 << "  BUY  " << order_size_ << " @ " << bid_price << "\n"
//                 << "  SELL " << order_size_ << " @ " << ask_price;
//     std::cout << mock_orders.str() << std::endl;
//     if (session_) session_->log.push_back(mock_orders.str());

//     if (session_) {
//         session_->executed_trades += 2;
//     }
// }

void MarketMakingStrategy::set_spread_bps(double spread_bps) {
    spread_bps_ = spread_bps;
    std::cout << "Spread updated to: " << spread_bps << " bps" << std::endl;
}

void MarketMakingStrategy::set_order_size(double size) {
    order_size_ = size;
    std::cout << "Order size updated to: " << size << std::endl;
}

bool MarketMakingStrategy::is_healthy() const {
    return redis_client_ && redis_client_->is_connected();
}

void MarketMakingStrategy::print_status() const {
    std::cout << "\nMarketMakingStrategy Status:" << std::endl;
    std::cout << "  Symbol: " << symbol_ << std::endl;
    std::cout << "  Exchange: " << exchange_ << std::endl;
    std::cout << "  Spread: " << spread_bps_ << " bps" << std::endl;
    std::cout << "  Order Size: " << order_size_ << std::endl;
    std::cout << "  Redis Connected: " << (is_healthy() ? "YES" : "NO") << std::endl;
}

std::string MarketMakingStrategy::get_redis_key() const {
    return "crypto:raw:" + exchange_ + ":" + symbol_;
}