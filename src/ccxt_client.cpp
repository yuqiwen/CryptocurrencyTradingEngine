#include "ccxt_client.h"
#include <iostream>
#include <sstream>

CCXTClient::CCXTClient(const std::string& base_url)
    : base_url_(base_url), timeout_seconds_(30), curl_(nullptr) {
}

CCXTClient::~CCXTClient() {
    if (curl_) {
        curl_easy_cleanup(curl_);
    }
    curl_global_cleanup();
}

bool CCXTClient::initialize() {
    // 初始化CURL
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
        std::cerr << "Failed to initialize CURL" << std::endl;
        return false;
    }
    
    curl_ = curl_easy_init();
    if (!curl_) {
        std::cerr << "Failed to initialize CURL handle" << std::endl;
        return false;
    }
    
    std::cout << "CCXT Client initialized with base URL: " << base_url_ << std::endl;
    return true;
}

// CURL写入回调函数
size_t CCXTClient::WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t totalSize = size * nmemb;
    userp->append((char*)contents, totalSize);
    return totalSize;
}

std::string CCXTClient::make_post_request(const std::string& endpoint, const json& payload) {
    if (!curl_) {
        std::cerr << "CURL not initialized" << std::endl;
        return "";
    }
    
    std::string url = base_url_ + endpoint;
    std::string response_data;
    std::string json_string = payload.dump();
    
    // 重置CURL选项
    curl_easy_reset(curl_);
    
    // 设置CURL选项
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, json_string.c_str());
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, json_string.length());
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response_data);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds_));
    
    // 设置HTTP头
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);
    
    std::cout << "POST " << url << std::endl;
    std::cout << "Payload: " << json_string << std::endl;
    
    // 执行请求
    CURLcode res = curl_easy_perform(curl_);
    curl_slist_free_all(headers);
    
    if (res != CURLE_OK) {
        std::cerr << "CURL request failed: " << curl_easy_strerror(res) << std::endl;
        return "";
    }
    
    long response_code;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &response_code);
    std::cout << "Response code: " << response_code << std::endl;
    std::cout << "Response: " << response_data << std::endl;
    
    return response_data;
}

std::string CCXTClient::make_get_request(const std::string& endpoint, const std::string& query_params) {
    if (!curl_) {
        std::cerr << "CURL not initialized" << std::endl;
        return "";
    }
    
    std::string url = base_url_ + endpoint;
    if (!query_params.empty()) {
        url += "?" + query_params;
    }
    
    std::string response_data;
    
    // 重置CURL选项
    curl_easy_reset(curl_);
    
    // 设置CURL选项
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response_data);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds_));
    
    std::cout << "GET " << url << std::endl;
    
    // 执行请求
    CURLcode res = curl_easy_perform(curl_);
    
    if (res != CURLE_OK) {
        std::cerr << "CURL request failed: " << curl_easy_strerror(res) << std::endl;
        return "";
    }
    
    long response_code;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &response_code);
    std::cout << "Response code: " << response_code << std::endl;
    std::cout << "Response: " << response_data << std::endl;
    
    return response_data;
}

OrderResult CCXTClient::place_limit_order(const std::string& exchange,
                                         const std::string& user_id,
                                         const std::string& symbol,
                                         const std::string& side,
                                         double amount,
                                         double price) {
    
    OrderResult result;
    result.success = false;
    
    json payload = {
        {"exchange", exchange},
        {"user_id", user_id},
        {"symbol", symbol},
        {"side", side},
        {"amount", amount},
        {"price", price}
    };
    
    std::cout << "Placing limit order: " << side << " " << amount << " " << symbol 
              << " @ " << price << " on " << exchange << std::endl;
    
    std::string response = make_post_request("/trade/order/limit", payload);
    
    if (response.empty()) {
        result.error_message = "No response from server";
        return result;
    }
    
    try {
        json response_json = json::parse(response);
        result.raw_response = response_json;
        
        // 检查是否有error字段
        if (response_json.contains("detail")) {
            result.error_message = response_json["detail"];
            std::cerr << "API Error: " << result.error_message << std::endl;
            return result;
        }
        
        // 成功的情况，提取订单ID
        if (response_json.contains("id")) {
            result.order_id = response_json["id"];
            result.success = true;
            std::cout << "Limit order placed successfully, ID: " << result.order_id << std::endl;
        } else {
            result.error_message = "Missing order ID in response";
        }
        
    } catch (const json::exception& e) {
        result.error_message = "Failed to parse JSON response: " + std::string(e.what());
        std::cerr << "JSON Parse Error: " << e.what() << std::endl;
        std::cerr << "Response was: " << response << std::endl;
    }
    
    return result;
}

OrderResult CCXTClient::place_market_order(const std::string& exchange,
                                          const std::string& user_id,
                                          const std::string& symbol,
                                          const std::string& side,
                                          double amount) {
    
    OrderResult result;
    result.success = false;
    
    json payload = {
        {"exchange", exchange},
        {"user_id", user_id},
        {"symbol", symbol},
        {"side", side},
        {"amount", amount}
    };
    
    std::cout << "Placing market order: " << side << " " << amount << " " << symbol 
              << " on " << exchange << std::endl;
    
    std::string response = make_post_request("/trade/order/market", payload);
    
    if (response.empty()) {
        result.error_message = "No response from server";
        return result;
    }
    
    try {
        json response_json = json::parse(response);
        result.raw_response = response_json;
        
        // 检查是否有error字段
        if (response_json.contains("detail")) {
            result.error_message = response_json["detail"];
            std::cerr << "API Error: " << result.error_message << std::endl;
            return result;
        }
        
        // 成功的情况，提取订单ID
        if (response_json.contains("id")) {
            result.order_id = response_json["id"];
            result.success = true;
            std::cout << "Market order placed successfully, ID: " << result.order_id << std::endl;
        } else {
            result.error_message = "Missing order ID in response";
        }
        
    } catch (const json::exception& e) {
        result.error_message = "Failed to parse JSON response: " + std::string(e.what());
        std::cerr << "JSON Parse Error: " << e.what() << std::endl;
        std::cerr << "Response was: " << response << std::endl;
    }
    
    return result;
}

bool CCXTClient::cancel_order(const std::string& exchange,
                             const std::string& user_id,
                             const std::string& symbol,
                             const std::string& order_id) {
    
    json payload = {
        {"exchange", exchange},
        {"user_id", user_id},
        {"symbol", symbol},
        {"order_id", order_id}
    };
    
    std::cout << "Cancelling order: " << order_id << " on " << exchange << std::endl;
    
    std::string response = make_post_request("/trade/order/cancel", payload);
    
    if (response.empty()) {
        std::cerr << "No response from server for cancel order" << std::endl;
        return false;
    }
    
    try {
        json response_json = json::parse(response);
        
        // 检查是否有error字段
        if (response_json.contains("detail")) {
            std::cerr << "Cancel order error: " << response_json["detail"] << std::endl;
            return false;
        }
        
        std::cout << "Order cancelled successfully: " << order_id << std::endl;
        return true;
        
    } catch (const json::exception& e) {
        std::cerr << "JSON Parse Error in cancel order: " << e.what() << std::endl;
        std::cerr << "Response was: " << response << std::endl;
        return false;
    }
}

OrderStatusResult CCXTClient::get_order_status(const std::string& exchange,
                                              const std::string& user_id,
                                              const std::string& symbol,
                                              const std::string& order_id) {
    
    OrderStatusResult result;
    result.success = false;
    
    // URL编码参数
    std::string query_params = "exchange=" + exchange + 
                              "&symbol=" + symbol + 
                              "&order_id=" + order_id + 
                              "&user_id=" + user_id;
    
    std::string response = make_get_request("/trade/order", query_params);
    
    if (response.empty()) {
        result.error_message = "No response from server";
        return result;
    }
    
    try {
        json response_json = json::parse(response);
        result.raw_response = response_json;
        
        // 检查是否有error字段
        if (response_json.contains("detail")) {
            result.error_message = response_json["detail"];
            return result;
        }
        
        // 提取订单状态信息
        if (response_json.contains("id") && response_json.contains("status")) {
            result.order_id = response_json["id"];
            result.status = response_json["status"];
            
            if (response_json.contains("filled")) {
                result.filled = response_json["filled"];
            }
            if (response_json.contains("remaining")) {
                result.remaining = response_json["remaining"];
            }
            
            result.success = true;
        } else {
            result.error_message = "Missing required fields in order status response";
        }
        
    } catch (const json::exception& e) {
        result.error_message = "Failed to parse JSON response: " + std::string(e.what());
        std::cerr << "JSON Parse Error: " << e.what() << std::endl;
        std::cerr << "Response was: " << response << std::endl;
    }
    
    return result;
}

BalanceResult CCXTClient::get_balance(const std::string& exchange,
                                    const std::string& user_id) {
    
    BalanceResult result;
    result.success = false;
    
    // 初始化所有余额为0
    result.btc_free = 0.0;
    result.btc_used = 0.0;
    result.btc_total = 0.0;
    result.usdt_free = 0.0;
    result.usdt_used = 0.0;
    result.usdt_total = 0.0;
    result.eth_free = 0.0;
    result.eth_used = 0.0;
    result.eth_total = 0.0;
    
    json payload = {
        {"exchange", exchange},
        {"user_id", user_id}
    };
    
    std::string response = make_post_request("/trade/balance", payload);
    
    if (response.empty()) {
        result.error_message = "No response from server";
        return result;
    }
    
    try {
        json response_json = json::parse(response);
        
        // 检查是否有error字段
        if (response_json.contains("detail")) {
            result.error_message = response_json["detail"];
            return result;
        }
        
        // 提取余额信息
        if (response_json.contains("BTC")) {
            auto btc = response_json["BTC"];
            if (btc.contains("free")) result.btc_free = btc["free"];
            if (btc.contains("used")) result.btc_used = btc["used"];
            if (btc.contains("total")) result.btc_total = btc["total"];
        }
        
        if (response_json.contains("USDT")) {
            auto usdt = response_json["USDT"];
            if (usdt.contains("free")) result.usdt_free = usdt["free"];
            if (usdt.contains("used")) result.usdt_used = usdt["used"];
            if (usdt.contains("total")) result.usdt_total = usdt["total"];
        }
        
        if (response_json.contains("ETH")) {
            auto eth = response_json["ETH"];
            if (eth.contains("free")) result.eth_free = eth["free"];
            if (eth.contains("used")) result.eth_used = eth["used"];
            if (eth.contains("total")) result.eth_total = eth["total"];
        }
        
        result.success = true;
        std::cout << "Balance retrieved successfully for " << exchange << std::endl;
        std::cout << "BTC: " << result.btc_free << " free, " << result.btc_total << " total" << std::endl;
        std::cout << "USDT: " << result.usdt_free << " free, " << result.usdt_total << " total" << std::endl;
        std::cout << "ETH: " << result.eth_free << " free, " << result.eth_total << " total" << std::endl;
        
    } catch (const json::exception& e) {
        result.error_message = "Failed to parse JSON response: " + std::string(e.what());
        std::cerr << "JSON Parse Error in get balance: " << e.what() << std::endl;
        std::cerr << "Response was: " << response << std::endl;
    }
    
    return result;
}