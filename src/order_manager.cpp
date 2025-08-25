#include "order_manager.h"
#include <iostream>
#include <sstream>
#include <random>
#include <iomanip>
#include <algorithm>

OrderManager::OrderManager(std::shared_ptr<CCXTClient> ccxt_client)
    : ccxt_client_(ccxt_client) {
    std::cout << "OrderManager initialized" << std::endl;
}

OrderManager::~OrderManager() {
    // 取消所有活跃订单
    cancel_expired_orders();
}

std::string OrderManager::generate_order_id() const {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(10000, 99999);
    
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::ostringstream oss;
    oss << "order_" << time_t << "_" << dis(gen);
    return oss.str();
}

std::string OrderManager::create_order(const std::string& session_id,
                                     const std::string& user_id,
                                     const std::string& exchange,
                                     const std::string& symbol,
                                     OrderSide side,
                                     OrderType type,
                                     double quantity,
                                     double price,
                                     int timeout_seconds) {
    
    std::string order_id = generate_order_id();
    auto order = std::make_unique<Order>();
    
    order->order_id = order_id;
    order->session_id = session_id;
    order->user_id = user_id;
    order->exchange = exchange;
    order->symbol = symbol;
    order->side = side;
    order->type = type;
    order->quantity = quantity;
    order->price = price;
    order->filled_quantity = 0.0;
    order->average_price = 0.0;
    order->status = OrderStatus::PENDING;
    order->created_at = std::chrono::system_clock::now();
    order->updated_at = order->created_at;
    order->expires_at = order->created_at + std::chrono::seconds(timeout_seconds);
    
    orders_[order_id] = std::move(order);
    
    std::cout << "Created order: " << order_id 
              << " (" << (side == OrderSide::BUY ? "BUY" : "SELL")
              << " " << quantity << " " << symbol 
              << " @ " << exchange << ")" << std::endl;
    
    log_order_activity(order_id, "Order created");
    return order_id;
}

bool OrderManager::submit_order(const std::string& order_id) {
    auto it = orders_.find(order_id);
    if (it == orders_.end()) {
        std::cerr << "Order not found: " << order_id << std::endl;
        return false;
    }
    
    auto& order = it->second;
    if (order->status != OrderStatus::PENDING) {
        std::cout << "Order already submitted: " << order_id << std::endl;
        return true;
    }
    
    std::cout << "Submitting order: " << order_id << std::endl;
    
    OrderResult result;
    
    if (order->type == OrderType::LIMIT) {
        result = ccxt_client_->place_limit_order(
            order->exchange,
            order->user_id,
            order->symbol,
            order->get_side_string(),
            order->quantity,
            order->price
        );
    } else {
        result = ccxt_client_->place_market_order(
            order->exchange,
            order->user_id,
            order->symbol,
            order->get_side_string(),
            order->quantity
        );
    }
    
    if (result.success) {
        order->exchange_order_id = result.order_id;
        order->status = OrderStatus::SUBMITTED;
        order->updated_at = std::chrono::system_clock::now();
        
        std::cout << "Order submitted successfully: " << order_id 
                  << " (exchange_id: " << result.order_id << ")" << std::endl;
        log_order_activity(order_id, "Order submitted to exchange");
        return true;
    } else {
        order->status = OrderStatus::FAILED;
        order->error_message = result.error_message;
        order->updated_at = std::chrono::system_clock::now();
        
        std::cerr << "Failed to submit order: " << order_id 
                  << " Error: " << result.error_message << std::endl;
        log_order_activity(order_id, "Order submission failed: " + result.error_message);
        return false;
    }
}

bool OrderManager::cancel_order(const std::string& order_id) {
    auto it = orders_.find(order_id);
    if (it == orders_.end()) {
        std::cerr << "Order not found: " << order_id << std::endl;
        return false;
    }
    
    auto& order = it->second;
    
    if (order->status != OrderStatus::SUBMITTED && order->status != OrderStatus::PARTIAL) {
        std::cout << "Order cannot be cancelled (status: " << static_cast<int>(order->status) << ")" << std::endl;
        return true;
    }
    
    if (order->exchange_order_id.empty()) {
        std::cerr << "No exchange order ID for cancellation - order was never submitted to exchange" << std::endl;
        return false;
    }
    
    std::cout << "Cancelling order: " << order_id 
              << " (exchange_order_id: " << order->exchange_order_id << ")" << std::endl;
    
    // 使用交易所的订单ID进行撤单
    bool success = ccxt_client_->cancel_order(
        order->exchange,
        order->user_id,
        order->symbol,
        order->exchange_order_id  // 重要：使用交易所的订单ID
    );
    
    if (success) {
        order->status = OrderStatus::CANCELLED;
        order->updated_at = std::chrono::system_clock::now();
        
        std::cout << "Order cancelled successfully: " << order_id 
                  << " (exchange_order_id: " << order->exchange_order_id << ")" << std::endl;
        log_order_activity(order_id, "Order cancelled at exchange");
        return true;
    } else {
        std::cerr << "Failed to cancel order: " << order_id << std::endl;
        log_order_activity(order_id, "Order cancellation failed at exchange");
        return false;
    }
}

bool OrderManager::update_order_status(const std::string& order_id) {
    auto it = orders_.find(order_id);
    if (it == orders_.end()) {
        return false;
    }
    
    auto& order = it->second;
    
    if (order->exchange_order_id.empty() || 
        (order->status != OrderStatus::SUBMITTED && order->status != OrderStatus::PARTIAL)) {
        return true;
    }
    
    OrderStatusResult result = ccxt_client_->get_order_status(
        order->exchange,
        order->user_id,
        order->symbol,
        order->exchange_order_id
    );
    
    if (result.success) {
        // 更新订单状态
        if (result.status == "closed") {
            order->status = OrderStatus::FILLED;
            order->filled_quantity = order->quantity;
        } else if (result.status == "canceled") {
            order->status = OrderStatus::CANCELLED;
        } else if (result.filled > 0 && result.filled < order->quantity) {
            order->status = OrderStatus::PARTIAL;
            order->filled_quantity = result.filled;
        }
        
        order->updated_at = std::chrono::system_clock::now();
        return true;
    }
    
    return false;
}

std::vector<std::string> OrderManager::create_arbitrage_orders(const std::string& session_id,
                                                             const std::string& user_id,
                                                             const std::string& symbol,
                                                             const std::string& buy_exchange,
                                                             const std::string& sell_exchange,
                                                             double quantity,
                                                             double buy_price,
                                                             double sell_price) {
    
    std::vector<std::string> order_ids;
    
    std::cout << "Creating arbitrage orders:" << std::endl;
    std::cout << "  BUY  " << quantity << " " << symbol << " @ " << buy_exchange << " price: " << buy_price << std::endl;
    std::cout << "  SELL " << quantity << " " << symbol << " @ " << sell_exchange << " price: " << sell_price << std::endl;
    
    // 创建买单
    std::string buy_order_id = create_order(
        session_id, user_id, buy_exchange, symbol,
        OrderSide::BUY, OrderType::LIMIT, quantity, buy_price
    );
    
    if (!buy_order_id.empty()) {
        order_ids.push_back(buy_order_id);
    }
    
    // 创建卖单
    std::string sell_order_id = create_order(
        session_id, user_id, sell_exchange, symbol,
        OrderSide::SELL, OrderType::LIMIT, quantity, sell_price
    );
    
    if (!sell_order_id.empty()) {
        order_ids.push_back(sell_order_id);
    }
    
    std::cout << "Created " << order_ids.size() << " arbitrage orders" << std::endl;
    return order_ids;
}

std::vector<std::string> OrderManager::create_market_making_orders(const std::string& session_id,
                                                                 const std::string& user_id,
                                                                 const std::string& exchange,
                                                                 const std::string& symbol,
                                                                 double quantity,
                                                                 double bid_price,
                                                                 double ask_price) {
    
    std::vector<std::string> order_ids;
    
    std::cout << "Creating market making orders:" << std::endl;
    std::cout << "  BID  " << quantity << " " << symbol << " @ " << bid_price << std::endl;
    std::cout << "  ASK  " << quantity << " " << symbol << " @ " << ask_price << std::endl;
    
    // 创建买单（bid）
    std::string bid_order_id = create_order(
        session_id, user_id, exchange, symbol,
        OrderSide::BUY, OrderType::LIMIT, quantity, bid_price
    );
    
    if (!bid_order_id.empty()) {
        order_ids.push_back(bid_order_id);
    }
    
    // 创建卖单（ask）
    std::string ask_order_id = create_order(
        session_id, user_id, exchange, symbol,
        OrderSide::SELL, OrderType::LIMIT, quantity, ask_price
    );
    
    if (!ask_order_id.empty()) {
        order_ids.push_back(ask_order_id);
    }
    
    std::cout << "Created " << order_ids.size() << " market making orders" << std::endl;
    return order_ids;
}

Order* OrderManager::get_order(const std::string& order_id) {
    auto it = orders_.find(order_id);
    return (it != orders_.end()) ? it->second.get() : nullptr;
}

Order* OrderManager::get_order_by_exchange_id(const std::string& exchange_order_id) {
    for (const auto& [order_id, order] : orders_) {
        if (order->exchange_order_id == exchange_order_id) {
            return order.get();
        }
    }
    return nullptr;
}

bool OrderManager::cancel_order_by_exchange_id(const std::string& exchange_order_id) {
    Order* order = get_order_by_exchange_id(exchange_order_id);
    if (!order) {
        std::cerr << "Order not found by exchange ID: " << exchange_order_id << std::endl;
        return false;
    }
    
    return cancel_order(order->order_id);
}

bool OrderManager::update_order_status_by_exchange_id(const std::string& exchange_order_id) {
    Order* order = get_order_by_exchange_id(exchange_order_id);
    if (!order) {
        std::cerr << "Order not found by exchange ID: " << exchange_order_id << std::endl;
        return false;
    }
    
    return update_order_status(order->order_id);
}

std::vector<Order*> OrderManager::get_orders_by_session(const std::string& session_id) {
    std::vector<Order*> session_orders;
    for (const auto& [order_id, order] : orders_) {
        if (order->session_id == session_id) {
            session_orders.push_back(order.get());
        }
    }
    return session_orders;
}

std::vector<Order*> OrderManager::get_active_orders() {
    std::vector<Order*> active_orders;
    for (const auto& [order_id, order] : orders_) {
        if (order->status == OrderStatus::SUBMITTED || order->status == OrderStatus::PARTIAL) {
            active_orders.push_back(order.get());
        }
    }
    return active_orders;
}

void OrderManager::update_all_orders() {
    for (const auto& [order_id, order] : orders_) {
        if (order->status == OrderStatus::SUBMITTED || order->status == OrderStatus::PARTIAL) {
            update_order_status(order_id);
        }
    }
}

void OrderManager::cancel_expired_orders() {
    auto now = std::chrono::system_clock::now();
    std::vector<std::string> orders_to_cancel;
    
    for (const auto& [order_id, order] : orders_) {
        if (order->should_cancel()) {
            orders_to_cancel.push_back(order_id);
        }
    }
    
    for (const auto& order_id : orders_to_cancel) {
        std::cout << "Cancelling expired order: " << order_id << std::endl;
        cancel_order(order_id);
    }
}

void OrderManager::cancel_session_orders(const std::string& session_id) {
    std::vector<std::string> orders_to_cancel;
    
    for (const auto& [order_id, order] : orders_) {
        if (order->session_id == session_id && 
            (order->status == OrderStatus::SUBMITTED || order->status == OrderStatus::PARTIAL)) {
            orders_to_cancel.push_back(order_id);
        }
    }
    
    for (const auto& order_id : orders_to_cancel) {
        std::cout << "Cancelling session order: " << order_id << std::endl;
        cancel_order(order_id);
    }
}

double OrderManager::get_session_profit(const std::string& session_id) const {
    double total_profit = 0.0;
    
    for (const auto& [order_id, order] : orders_) {
        if (order->session_id == session_id && order->status == OrderStatus::FILLED) {
            if (order->side == OrderSide::SELL) {
                total_profit += order->get_filled_amount();
            } else {
                total_profit -= order->get_filled_amount();
            }
        }
    }
    
    return total_profit;
}

int OrderManager::get_session_trades(const std::string& session_id) const {
    int trade_count = 0;
    
    for (const auto& [order_id, order] : orders_) {
        if (order->session_id == session_id && order->status == OrderStatus::FILLED) {
            trade_count++;
        }
    }
    
    return trade_count;
}

bool OrderManager::check_balance(const std::string& exchange, const std::string& user_id,
                                const std::string& symbol, OrderSide side, double quantity, double price) {
    
    BalanceResult balance = ccxt_client_->get_balance(exchange, user_id);
    
    if (!balance.success) {
        std::cerr << "Failed to get balance for balance check" << std::endl;
        return false;
    }
    
    // 简化的余额检查逻辑
    if (side == OrderSide::BUY) {
        // 买单需要足够的报价货币（如USDT）
        double required_amount = quantity * price;
        if (symbol.find("USDT") != std::string::npos) {
            return balance.usdt_free >= required_amount;
        }
        // 可以添加更多货币的检查
    } else {
        // 卖单需要足够的基础货币（如BTC）
        if (symbol.find("BTC") != std::string::npos) {
            return balance.btc_free >= quantity;
        } else if (symbol.find("ETH") != std::string::npos) {
            return balance.eth_free >= quantity;
        }
        // 可以添加更多货币的检查
    }
    
    return true; // 默认通过（实际应该更严格）
}

void OrderManager::print_session_orders(const std::string& session_id) const {
    std::cout << "\n=== Orders for Session: " << session_id << " ===" << std::endl;
    
    std::vector<Order*> session_orders;
    for (const auto& [order_id, order] : orders_) {
        if (order->session_id == session_id) {
            session_orders.push_back(order.get());
        }
    }
    
    if (session_orders.empty()) {
        std::cout << "No orders found for this session." << std::endl;
        return;
    }
    
    for (const auto& order : session_orders) {
        std::cout << "Internal Order ID: " << order->order_id << std::endl;
        if (!order->exchange_order_id.empty()) {
            std::cout << "Exchange Order ID: " << order->exchange_order_id << std::endl;
        } else {
            std::cout << "Exchange Order ID: [NOT SUBMITTED YET]" << std::endl;
        }
        std::cout << "  Exchange: " << order->exchange << std::endl;
        std::cout << "  Symbol: " << order->symbol << std::endl;
        std::cout << "  Side: " << (order->side == OrderSide::BUY ? "BUY" : "SELL") << std::endl;
        std::cout << "  Type: " << (order->type == OrderType::LIMIT ? "LIMIT" : "MARKET") << std::endl;
        std::cout << "  Quantity: " << order->quantity << std::endl;
        std::cout << "  Price: " << order->price << std::endl;
        std::cout << "  Status: " << static_cast<int>(order->status) << std::endl;
        std::cout << "  Filled: " << order->filled_quantity << std::endl;
        if (!order->error_message.empty()) {
            std::cout << "  Error: " << order->error_message << std::endl;
        }
        std::cout << "---" << std::endl;
    }
}

void OrderManager::log_order_activity(const std::string& order_id, const std::string& message) const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::cout << "[" << std::put_time(std::localtime(&time_t), "%H:%M:%S") << "] "
              << "Order " << order_id << ": " << message << std::endl;
}