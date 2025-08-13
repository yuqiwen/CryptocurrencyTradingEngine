#include "../include/arbitrage_strategy.h"
#include "../include/trading_engine_manager.h"
#include <iostream>
#include <iomanip>
#include <algorithm>

ArbitrageStrategy::ArbitrageStrategy(std::shared_ptr<RedisWriter> redis_client,
                                     const std::string& symbol)
    : redis_client_(redis_client), symbol_(symbol){
    
    if (symbol == "BTC/USDT") {
        min_profit_bps_ = 20.0;
        max_trade_size_ = 8000.0;
    } else if (symbol == "ETH/USDT") {
        min_profit_bps_ = 25.0;
        max_trade_size_ = 6000.0;
    } else {
        min_profit_bps_ = 30.0;
        max_trade_size_ = 4000.0;
    }

    std::cout << "ArbitrageStrategy created for symbol: " << symbol_ << std::endl;
    std::cout << "Min profit threshold: " << min_profit_bps_ << " bps, "
              << "Max trade size: $" << max_trade_size_ << std::endl;
}

double ArbitrageStrategy::get_exchange_fee(const std::string& exchange) {
    if (exchange == "bitmart") {
        return 25.0;
    } else if (exchange == "cryptocom") {
        return 40.0;
    } else if (exchange == "mexc") {
        return 20.0;
    } else {
        return 30.0;
    }
}

StrategyResult ArbitrageStrategy::run_once() {
    StrategyResult result;

    std::ostringstream header;
    header << "\n=== Arbitrage Opportunity Scan ===";
    std::cout << header.str() << std::endl;
    result.logs.push_back(header.str());

    PriceStatsRecord stats_record;
    bool success = redis_client_->read_price_stats_record(symbol_, stats_record);
    
    if (!success) {
        std::string msg1 = "Failed to read price stats from Redis for " + symbol_;
        std::string msg2 = "Make sure DataSyncService is running and syncing price stats";

        std::cout << msg1 << std::endl;
        std::cout << msg2 << std::endl;

        result.logs.push_back(msg1);
        result.logs.push_back(msg2);
        return result;
    }

    std::ostringstream oss;
    oss << "Price Stats for " << symbol_ << ":\n"
        << "  Highest: $" << stats_record.highest_price << " @ " << stats_record.highest_exchange << "\n"
        << "  Lowest:  $" << stats_record.lowest_price << " @ " << stats_record.lowest_exchange << "\n"
        << "  Price Spread: $" << (stats_record.highest_price - stats_record.lowest_price) << "\n"
        << "  Records: " << stats_record.record_count << " exchanges analyzed";

    std::cout << oss.str() << std::endl;
    result.logs.push_back(oss.str());

    ArbitrageOpportunity opportunity = analyze_price_stats_arbitrage(stats_record);

    std::ostringstream summary;
    if (opportunity.is_profitable) {
        double net_profit = (opportunity.net_profit_bps / 10000.0) *
                            opportunity.buy_price * opportunity.max_quantity;

        result.profit = net_profit;
        result.trades = 2;

        summary << "[Arbitrage] Opportunity: Buy @ " << opportunity.buy_price << " (" << opportunity.buy_exchange
                << "), Sell @ " << opportunity.sell_price << " (" << opportunity.sell_exchange << ")\n"
                << "Net Profit: $" << std::fixed << std::setprecision(2) << net_profit
                << " | Net bps: " << opportunity.net_profit_bps;
    } else {
        summary << "No arbitrage opportunity found. Reason: " << opportunity.reason;
    }

    std::cout << summary.str() << std::endl;
    result.logs.push_back(summary.str());

    return result;
}



ArbitrageStrategy::ArbitrageOpportunity ArbitrageStrategy::analyze_price_stats_arbitrage(const PriceStatsRecord& stats) {
    ArbitrageOpportunity opportunity;
    opportunity.is_profitable = false;

    opportunity.buy_exchange = stats.lowest_exchange;
    opportunity.sell_exchange = stats.highest_exchange;
    opportunity.buy_price = stats.lowest_price;
    opportunity.sell_price = stats.highest_price;

    opportunity.gross_profit_bps = (opportunity.sell_price - opportunity.buy_price) / opportunity.buy_price * 10000;
    opportunity.net_profit_bps = calculate_net_profit_bps(
        opportunity.buy_price, opportunity.sell_price,
        opportunity.buy_exchange, opportunity.sell_exchange
    );

    if (opportunity.net_profit_bps >= min_profit_bps_) {
        opportunity.is_profitable = true;
        opportunity.max_quantity = max_trade_size_ / opportunity.buy_price;
        std::cout << "SUCCESS: Found profitable arbitrage opportunity!" << std::endl;
    } else {
        opportunity.reason = "Net profit (" + std::to_string(opportunity.net_profit_bps) +
                             "bps) below minimum (" + std::to_string(min_profit_bps_) + "bps)";
    }

    return opportunity;
}

double ArbitrageStrategy::calculate_net_profit_bps(double buy_price, double sell_price,
                                                   const std::string& buy_exchange,
                                                   const std::string& sell_exchange) {
    if (buy_price <= 0 || sell_price <= 0) return -1000.0;

    double buy_fee_bps = get_exchange_fee(buy_exchange);
    double sell_fee_bps = get_exchange_fee(sell_exchange);

    double buy_fee = buy_price * buy_fee_bps / 10000.0;
    double sell_fee = sell_price * sell_fee_bps / 10000.0;

    double net_profit = (sell_price - buy_price) - (buy_fee + sell_fee);
    return (net_profit / buy_price) * 10000.0;
}

// void ArbitrageStrategy::print_opportunity(const ArbitrageOpportunity& opportunity) {
//     std::ostringstream log;

//     log << "\n=== Arbitrage Analysis ===\n";

//     if (!opportunity.is_profitable) {
//         log << "No profitable arbitrage opportunity found\n";
//         if (!opportunity.reason.empty()) {
//             log << "Reason: " << opportunity.reason << "\n";
//         }

//         std::cout << log.str();
//         if (session_) session_->log.push_back(log.str());
//         return;
//     }
//     double profit = (opportunity.net_profit_bps / 10000.0)
//                 * opportunity.buy_price
//                 * opportunity.max_quantity;
//     log << "PROFITABLE ARBITRAGE FOUND!\n";
//     log << "Strategy:\n";
//     log << "  1. BUY  at " << opportunity.buy_exchange << " @ " << opportunity.buy_price << "\n";
//     log << "  2. SELL at " << opportunity.sell_exchange << " @ " << opportunity.sell_price << "\n";

//     log << "\nProfit Analysis:\n";
//     log << std::fixed << std::setprecision(2);
//     log << "  Gross Profit: " << opportunity.gross_profit_bps << " bps\n";
//     log << "  Net Profit:   " << opportunity.net_profit_bps << " bps (after fees)\n";
//     log << "  Max Quantity: " << opportunity.max_quantity << " " << symbol_.substr(0, 3) << "\n";
//     log << "  Max Profit:   $"
//         << profit
//         << "\n";

//     log << "\nRisk Factors:\n";
//     log << "  - Execution timing risk\n";
//     log << "  - Slippage risk\n";
//     log << "  - Transfer time between exchanges\n";

//     std::cout << log.str();

//     if (session_) 
//     {
//         session_->total_profit += profit;  
//         session_->executed_trades += 1;
//         session_->log.push_back(log.str());
//     }
// }


void ArbitrageStrategy::set_min_profit_bps(double min_profit_bps) {
    min_profit_bps_ = min_profit_bps;
    std::cout << "Min profit updated to: " << min_profit_bps << " bps" << std::endl;
}

void ArbitrageStrategy::set_max_trade_size(double max_size) {
    max_trade_size_ = max_size;
    std::cout << "Max trade size updated to: $" << max_size << std::endl;
}

bool ArbitrageStrategy::is_healthy() const {
    return redis_client_ && redis_client_->is_connected();
}

void ArbitrageStrategy::print_status() const {
    std::cout << "\nArbitrageStrategy Status:" << std::endl;
    std::cout << "  Symbol: " << symbol_ << std::endl;
    std::cout << "  Min Profit: " << min_profit_bps_ << " bps" << std::endl;
    std::cout << "  Max Trade Size: $" << max_trade_size_ << std::endl;
    std::cout << "  Redis Connected: " << (is_healthy() ? "YES" : "NO") << std::endl;
}

std::string ArbitrageStrategy::get_redis_key(const std::string& exchange) const {
    return "crypto:raw:" + exchange + ":" + symbol_;
}
