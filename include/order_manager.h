#pragma once


#include "ccxt_client.h"
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <memory>
#include <iostream>
#include <iomanip>

enum class OrderSide {
    BUY,
    SELL
};

enum class OrderType {
    MARKET,
    LIMIT
};

enum class OrderStatus {
    PENDING,     // 订单已创建，待发送
    SUBMITTED,   // 已提交到交易所
    PARTIAL,     // 部分成交
    FILLED,      // 完全成交
    CANCELLED,   // 已取消
    FAILED,      // 失败
    EXPIRED      // 超时
};

struct Order {
    std::string order_id;
    std::string session_id;        // 所属交易会话
    std::string user_id;          // 用户ID
    std::string exchange;
    std::string symbol;
    OrderSide side;
    OrderType type;
    double quantity;
    double price;                  // 限价单价格，市价单为0
    double filled_quantity;        // 已成交数量
    double average_price;          // 平均成交价格
    OrderStatus status;
    std::string exchange_order_id; // 交易所返回的订单ID
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;
    std::chrono::system_clock::time_point expires_at;
    std::string error_message;
    
    // 计算已成交金额
    double get_filled_amount() const {
        return filled_quantity * average_price;
    }
    
    // 检查是否需要撤单
    bool should_cancel() const {
        auto now = std::chrono::system_clock::now();
        return (status == OrderStatus::SUBMITTED || status == OrderStatus::PARTIAL) && 
               now > expires_at;
    }
    
    // 转换为API调用参数
    std::string get_side_string() const {
        return (side == OrderSide::BUY) ? "buy" : "sell";
    }
};

class OrderManager {
public:
    OrderManager(std::shared_ptr<CCXTClient> ccxt_client);
    ~OrderManager();
    
    // 订单操作
    std::string create_order(const std::string& session_id,
                           const std::string& user_id,
                           const std::string& exchange,
                           const std::string& symbol,
                           OrderSide side,
                           OrderType type,
                           double quantity,
                           double price = 0.0,
                           int timeout_seconds = 300);
    
    bool submit_order(const std::string& order_id);
    bool cancel_order(const std::string& order_id);
    bool update_order_status(const std::string& order_id);
    
    // 批量操作 - 套利订单
    std::vector<std::string> create_arbitrage_orders(const std::string& session_id,
                                                   const std::string& user_id,
                                                   const std::string& symbol,
                                                   const std::string& buy_exchange,
                                                   const std::string& sell_exchange,
                                                   double quantity,
                                                   double buy_price,
                                                   double sell_price);
    
    // 批量操作 - 做市订单
    std::vector<std::string> create_market_making_orders(const std::string& session_id,
                                                       const std::string& user_id,
                                                       const std::string& exchange,
                                                       const std::string& symbol,
                                                       double quantity,
                                                       double bid_price,
                                                       double ask_price);
    
    // 订单查询
    Order* get_order(const std::string& order_id);
    Order* get_order_by_exchange_id(const std::string& exchange_order_id);
    std::vector<Order*> get_orders_by_session(const std::string& session_id);
    std::vector<Order*> get_active_orders();
    
    // 订单操作 - 支持通过交易所订单ID操作
    bool cancel_order_by_exchange_id(const std::string& exchange_order_id);
    bool update_order_status_by_exchange_id(const std::string& exchange_order_id);
    
    // 订单管理
    void update_all_orders();        // 更新所有活跃订单状态
    void cancel_expired_orders();    // 取消过期订单
    void cancel_session_orders(const std::string& session_id); // 取消会话的所有订单
    
    // 统计信息
    double get_session_profit(const std::string& session_id) const;
    int get_session_trades(const std::string& session_id) const;
    
    // 风险控制
    bool check_balance(const std::string& exchange, const std::string& user_id, 
                      const std::string& symbol, OrderSide side, double quantity, double price);
    
    // 状态显示
    void print_session_orders(const std::string& session_id) const;

private:
    std::shared_ptr<CCXTClient> ccxt_client_;
    std::map<std::string, std::unique_ptr<Order>> orders_;
    
    // 内部方法
    std::string generate_order_id() const;
    void log_order_activity(const std::string& order_id, const std::string& message) const;
};
