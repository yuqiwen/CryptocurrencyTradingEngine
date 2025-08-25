#pragma once
#include <string>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <iostream>

using json = nlohmann::json;

struct OrderResult {
    bool success;
    std::string order_id;
    std::string error_message;
    json raw_response;
};

struct BalanceResult {
    bool success;
    double btc_free;
    double btc_used;
    double btc_total;
    double usdt_free;
    double usdt_used;
    double usdt_total;
    double eth_free;
    double eth_used;
    double eth_total;
    std::string error_message;
};

struct OrderStatusResult {
    bool success;
    std::string order_id;
    std::string status;     // open, closed, canceled
    double filled;
    double remaining;
    std::string error_message;
    json raw_response;
};

class CCXTClient {
public:
    CCXTClient(const std::string& base_url = "http://localhost:8000");
    ~CCXTClient();
    
    bool initialize();
    
    // 下单接口
    OrderResult place_limit_order(const std::string& exchange,
                                const std::string& user_id,
                                const std::string& symbol,
                                const std::string& side,
                                double amount,
                                double price);
    
    OrderResult place_market_order(const std::string& exchange,
                                 const std::string& user_id,
                                 const std::string& symbol,
                                 const std::string& side,
                                 double amount);
    
    // 撤单接口
    bool cancel_order(const std::string& exchange,
                     const std::string& user_id,
                     const std::string& symbol,
                     const std::string& order_id);
    
    // 查询订单
    OrderStatusResult get_order_status(const std::string& exchange,
                                     const std::string& user_id,
                                     const std::string& symbol,
                                     const std::string& order_id);
    
    // 获取余额
    BalanceResult get_balance(const std::string& exchange,
                            const std::string& user_id);
    
    // 配置
    void set_base_url(const std::string& url) { base_url_ = url; }
    void set_timeout(int timeout_seconds) { timeout_seconds_ = timeout_seconds; }

private:
    std::string base_url_;
    int timeout_seconds_;
    CURL* curl_;
    
    // HTTP请求辅助方法
    std::string make_post_request(const std::string& endpoint, const json& payload);
    std::string make_get_request(const std::string& endpoint, const std::string& query_params = "");
    
    // CURL回调函数
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
};

