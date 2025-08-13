#include "trading_engine_manager.h"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    // === 1. 数据库和Redis配置 ===
    std::string db_conninfo = "host=localhost dbname=crypto user=postgres password=123";
    std::string redis_host = "127.0.0.1";
    int redis_port = 6379;
    std::string redis_password = ""; // 若无密码，留空

    // === 2. 创建交易引擎 ===
    TradingEngineManager engine(db_conninfo, redis_host, redis_port, redis_password);

    if (!engine.initialize()) {
        std::cerr << "[FATAL] 引擎初始化失败，程序退出。" << std::endl;
        return 1;
    }

    engine.start_engine();

    // === 3. 构造一个测试交易请求（可改为 ARBITRAGE / MARKET_MAKING / MIXED）===
    ClientRequest req;
    req.client_id = "test_client";
    req.symbol = "BTC/USDT";
    req.mode = TradingMode::MIXED;  // 支持 MARKET_MAKING / ARBITRAGE / MIXED
    req.exchange = "bitmart";       // 做市必须填 exchange
    req.max_amount = 1000.0;        // 美元为单位
    req.target_profit = 25.0;       // 单位：bps（千分之一）

    // === 4. 创建并启动交易会话 ===
    std::string session_id = engine.create_trading_session(req);
    if (session_id.empty()) {
        std::cerr << "[ERROR] 会话创建失败" << std::endl;
        return 1;
    }

    if (!engine.start_trading_session(session_id)) {
        std::cerr << "[ERROR] 启动会话失败" << std::endl;
        return 1;
    }

    std::cout << "\n 交易系统已启动，正在运行会话：" << session_id << std::endl;
    std::cout << " 策略正在运行中（每秒执行一次）...\n" << std::endl;

    // === 5. 阻塞主线程（保持服务运行）===
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }

    return 0;
}
