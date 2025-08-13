#include "../include/trading_engine_manager.h"
#include <iostream>
#include <random>
#include <sstream>
#include <iomanip>  

// 构造函数
TradingEngineManager::TradingEngineManager(const std::string& db_conninfo,
                                         const std::string& redis_host,
                                         int redis_port,
                                         const std::string& redis_password)
    : engine_status_(EngineStatus::STOPPED), 
      should_run_(false), 
      trading_interval_ms_(5000), 
      max_sessions_(10) {
    
    // 创建Redis客户端（共享指针，因为要传给策略）
    redis_client_ = std::make_shared<RedisWriter>(redis_host, redis_port, redis_password);
    
    // 创建数据同步服务
    data_sync_service_ = std::make_unique<DataSyncService>(db_conninfo, redis_host, redis_port, redis_password);
    
    // 重置统计信息
    reset_stats();
    
    std::cout << "TradingEngineManager constructed" << std::endl;
}

// 析构函数
TradingEngineManager::~TradingEngineManager() {
    shutdown();
}

// 初始化函数
bool TradingEngineManager::initialize() {
    std::cout << "Initializing trading engine manager..." << std::endl;
    
    // 检查Redis连接
    if (!redis_client_ || !redis_client_->is_connected()) {
        std::cerr << "Redis connection failed" << std::endl;
        engine_status_ = EngineStatus::ERROR;
        return false;
    }
    std::cout << "Redis connected" << std::endl;
    
    // 检查数据同步服务
    if (!data_sync_service_ || !data_sync_service_->is_healthy()) {
        std::cerr << "Data sync service not healthy" << std::endl;
        engine_status_ = EngineStatus::ERROR;
        return false;
    }
    std::cout << "Data sync service healthy" << std::endl;
    
    engine_status_ = EngineStatus::STOPPED;
    std::cout << "Trading engine manager initialized successfully" << std::endl;
    return true;
}

// 关闭函数
void TradingEngineManager::shutdown() {
    std::cout << "Shutting down trading engine..." << std::endl;
    
    // 停止引擎
    stop_engine();
    
    // 清理所有会话
    trading_sessions_.clear();
    
    engine_status_ = EngineStatus::STOPPED;
    std::cout << "Trading engine shutdown complete" << std::endl;
}

// 健康检查
bool TradingEngineManager::is_healthy() const {
    return engine_status_ != EngineStatus::ERROR && 
           redis_client_ && redis_client_->is_connected() &&
           data_sync_service_ && data_sync_service_->is_healthy();
}

// 重置统计信息
void TradingEngineManager::reset_stats() {
    stats_.total_sessions_created = 0;
    stats_.active_sessions = 0;
    stats_.total_trades_executed = 0;
    stats_.total_profit_generated = 0.0;
    stats_.engine_start_time = std::chrono::system_clock::now();
    stats_.last_update_time = stats_.engine_start_time;
}


// 验证客户请求
bool TradingEngineManager::validate_client_request(const ClientRequest& request) const {
    if (request.client_id.empty()) {
        std::cerr << "Client ID cannot be empty" << std::endl;
        return false;
    }
    
    if (request.symbol.empty()) {
        std::cerr << "Symbol cannot be empty" << std::endl;
        return false;
    }
    
    if (request.max_amount <= 0) {
        std::cerr << "Max amount must be positive" << std::endl;
        return false;
    }
    
    if (request.target_profit <= 0) {
        std::cerr << "Target profit must be positive" << std::endl;
        return false;
    }
    
    if (trading_sessions_.size() >= static_cast<size_t>(max_sessions_)) {
        std::cerr << "Maximum number of sessions reached (" << max_sessions_ << ")" << std::endl;
        return false;
    }
    
    // 如果是做市模式，需要指定交易所
    if ((request.mode == TradingMode::MARKET_MAKING || request.mode == TradingMode::MIXED) 
        && request.exchange.empty()) {
        std::cerr << "Exchange must be specified for market making mode" << std::endl;
        return false;
    }
    
    std::cout << "Client request validation passed" << std::endl;
    return true;
}

void TradingEngineManager::log_session_activity(const std::string& session_id, const std::string& message) const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::cout << "[" << std::put_time(std::localtime(&time_t), "%H:%M:%S") << "] "
              << "Session " << session_id << ": " << message << std::endl;
}

// 创建交易会话
std::string TradingEngineManager::create_trading_session(const ClientRequest& request) {
    std::cout << "\n=== Creating Trading Session ===" << std::endl;
    
    // 验证请求
    if (!validate_client_request(request)) {
        return "";
    }
    
    // 生成会话ID
    std::string session_id = generate_session_id();
    
    // 创建新会话
    auto session = std::make_unique<TradingSession>();
    session->session_id = session_id;
    session->request = request;
    session->status = EngineStatus::STOPPED;
    session->total_profit = 0.0;
    session->executed_trades = 0;
    session->created_at = std::chrono::system_clock::now();
    session->last_update = session->created_at;
    
    // 根据交易模式初始化策略
    if (request.mode == TradingMode::ARBITRAGE || request.mode == TradingMode::MIXED) {
        std::cout << "Initializing arbitrage strategy..." << std::endl;
        session->arbitrage_strategy = std::make_unique<ArbitrageStrategy>(
            redis_client_,
            request.symbol
        );
        session->arbitrage_strategy->set_min_profit_bps(request.target_profit);
        session->arbitrage_strategy->set_max_trade_size(request.max_amount);
        std::cout << "Arbitrage strategy initialized" << std::endl;
    }
    
    if (request.mode == TradingMode::MARKET_MAKING || request.mode == TradingMode::MIXED) {
        std::cout << "Initializing market making strategy..." << std::endl;
        session->market_making_strategy = std::make_unique<MarketMakingStrategy>(
            redis_client_,
            request.symbol,
            request.exchange
        );
        
        // 根据目标利润设置价差 (做市策略的价差应该小于目标利润)
        double spread_bps = std::max(5.0, request.target_profit / 2.0);
        session->market_making_strategy->set_spread_bps(spread_bps);
        
        // 根据金额计算订单大小
        double order_size = request.max_amount / 1000.0; // 简单计算，可以优化
        session->market_making_strategy->set_order_size(order_size);
        std::cout << "Market making strategy initialized" << std::endl;
    }
    
    // 存储会话
    trading_sessions_[session_id] = std::move(session);
    stats_.total_sessions_created++;
    
    std::cout << "Trading session created successfully!" << std::endl;
    std::cout << "Session ID: " << session_id << std::endl;
    std::cout << "Client: " << request.client_id << std::endl;
    std::cout << "Symbol: " << request.symbol << std::endl;
    std::cout << "Mode: " << (request.mode == TradingMode::ARBITRAGE ? "Arbitrage" : 
                              request.mode == TradingMode::MARKET_MAKING ? "Market Making" : "Mixed") << std::endl;
    std::cout << "Max Amount: $" << request.max_amount << std::endl;
    std::cout << "Target Profit: " << request.target_profit << " bps" << std::endl;
    
    log_session_activity(session_id, "Session created for client: " + request.client_id);
    
    return session_id;
}

// 启动交易会话
bool TradingEngineManager::start_trading_session(const std::string& session_id) {
    std::cout << "\n=== Starting Trading Session ===" << std::endl;
    
    auto it = trading_sessions_.find(session_id);
    if (it == trading_sessions_.end()) {
        std::cerr << "Session not found: " << session_id << std::endl;
        return false;
    }
    
    auto& session = it->second;
    
    // 检查当前状态
    if (session->status == EngineStatus::RUNNING) {
        std::cout << "Session already running: " << session_id << std::endl;
        return true;
    }
    
    session->status = EngineStatus::STARTING;
    std::cout << "Starting session: " << session_id << std::endl;
    
    // 检查策略健康状态
    bool healthy = true;
    
    if (session->arbitrage_strategy) {
        if (!session->arbitrage_strategy->is_healthy()) {
            std::cerr << "Arbitrage strategy not healthy" << std::endl;
            healthy = false;
        } else {
            std::cout << "Arbitrage strategy healthy" << std::endl;
        }
    }
    
    if (session->market_making_strategy) {
        if (!session->market_making_strategy->is_healthy()) {
            std::cerr << "Market making strategy not healthy" << std::endl;
            healthy = false;
        } else {
            std::cout << "Market making strategy healthy" << std::endl;
        }
    }
    
    if (!healthy) {
        session->status = EngineStatus::ERROR;
        log_session_activity(session_id, "Failed to start - strategy health check failed");
        return false;
    }
    
    // 启动成功
    session->status = EngineStatus::RUNNING;
    session->last_update = std::chrono::system_clock::now();
    stats_.active_sessions++;
    
    std::cout << "Trading session started successfully!" << std::endl;
    log_session_activity(session_id, "Session started and running");
    
    return true;
}

// 停止交易会话
bool TradingEngineManager::stop_trading_session(const std::string& session_id) {
    std::cout << "\n=== Stopping Trading Session ===" << std::endl;
    
    auto it = trading_sessions_.find(session_id);
    if (it == trading_sessions_.end()) {
        std::cerr << "Session not found: " << session_id << std::endl;
        return false;
    }
    
    auto& session = it->second;
    
    // 检查当前状态
    if (session->status != EngineStatus::RUNNING) {
        std::cout << "Session not running: " << session_id << std::endl;
        return true;
    }
    
    session->status = EngineStatus::STOPPING;
    std::cout << "Stopping session: " << session_id << std::endl;
    
    // 这里可以添加清理逻辑
    // 比如取消挂单、平仓等
    // TODO: 实现具体的清理逻辑
    
    // 停止成功
    session->status = EngineStatus::STOPPED;
    session->last_update = std::chrono::system_clock::now();
    stats_.active_sessions--;
    
    std::cout << "Trading session stopped successfully!" << std::endl;
    log_session_activity(session_id, "Session stopped");
    
    return true;
}

// 删除交易会话
bool TradingEngineManager::remove_trading_session(const std::string& session_id) {
    std::cout << "\n=== Removing Trading Session ===" << std::endl;
    
    auto it = trading_sessions_.find(session_id);
    if (it == trading_sessions_.end()) {
        std::cerr << "Session not found: " << session_id << std::endl;
        return false;
    }
    
    // 确保会话已停止
    if (it->second->status == EngineStatus::RUNNING) {
        std::cout << "Stopping session before removal..." << std::endl;
        stop_trading_session(session_id);
    }
    
    std::cout << "Removing session: " << session_id << std::endl;
    trading_sessions_.erase(it);
    
    std::cout << "Trading session removed successfully!" << std::endl;
    log_session_activity(session_id, "Session removed");
    
    return true;
}

// 获取活跃会话列表
std::vector<std::string> TradingEngineManager::get_active_sessions() const {
    std::vector<std::string> sessions;
    for (const auto& pair : trading_sessions_) {
        sessions.push_back(pair.first);
    }
    return sessions;
}

// 获取指定会话
TradingSession* TradingEngineManager::get_session(const std::string& session_id) {
    auto it = trading_sessions_.find(session_id);
    return (it != trading_sessions_.end()) ? it->second.get() : nullptr;
}

// 更新会话统计信息
void TradingEngineManager::update_session_stats(TradingSession* session, double profit, int trades) {
    if (session) {
        session->total_profit += profit;
        session->executed_trades += trades;
        session->last_update = std::chrono::system_clock::now();
        
        // 更新全局统计
        stats_.total_profit_generated += profit;
        stats_.total_trades_executed += trades;
        stats_.last_update_time = session->last_update;
    }
}

// 启动引擎
void TradingEngineManager::start_engine() {
    if (engine_status_ == EngineStatus::RUNNING) {
        std::cout << "Engine already running" << std::endl;
        return;
    }
    
    std::cout << "Starting trading engine..." << std::endl;
    engine_status_ = EngineStatus::STARTING;
    should_run_ = true;
    
    // 启动数据同步调度器
    data_sync_service_->start_scheduler();
    data_sync_service_->schedule_sync_task(5000); // 5秒同步一次
    
    // 启动交易循环线程
    engine_thread_ = std::make_unique<std::thread>(&TradingEngineManager::trading_loop, this);
    
    engine_status_ = EngineStatus::RUNNING;
    stats_.engine_start_time = std::chrono::system_clock::now();
    
    std::cout << "Trading engine started successfully" << std::endl;
}

// 停止引擎
void TradingEngineManager::stop_engine() {
    if (engine_status_ == EngineStatus::STOPPED) {
        std::cout << "Engine already stopped" << std::endl;
        return;
    }
    
    std::cout << "Stopping trading engine..." << std::endl;
    engine_status_ = EngineStatus::STOPPING;
    should_run_ = false;
    
    // 等待交易线程结束
    if (engine_thread_ && engine_thread_->joinable()) {
        engine_thread_->join();
    }
    
    // 停止数据同步服务
    if (data_sync_service_) {
        data_sync_service_->stop_scheduler();
    }
    
    // 停止所有活跃会话
    for (auto& [session_id, session] : trading_sessions_) {
        if (session->status == EngineStatus::RUNNING) {
            stop_trading_session(session_id);
        }
    }
    
    engine_status_ = EngineStatus::STOPPED;
    std::cout << "Trading engine stopped" << std::endl;
}

// 交易循环
void TradingEngineManager::trading_loop() {
    std::cout << "Trading loop started" << std::endl;
    
    while (should_run_) {
        try {
            // 检查所有运行中的会话
            for (auto& [session_id, session] : trading_sessions_) {
                if (session->status == EngineStatus::RUNNING) {
                    execute_trading_session(session.get());
                    session->last_update = std::chrono::system_clock::now();
                    // === 止盈止损判断逻辑 ===
                    double profit = session->total_profit;
                    double max_amount = session->request.max_amount;
                    double take_profit_amount = max_amount * session->request.take_profit_ratio;
                    double stop_loss_amount = max_amount * session->request.stop_loss_ratio;

                    if (profit >= take_profit_amount) {
                        std::cout << "[止盈] Session " << session_id 
                                  << " 盈利 $" << profit << "，自动停止" << std::endl;
                        stop_trading_session(session_id);
                        log_session_activity(session_id, "止盈触发，自动停止");
                    } else if (profit <= -stop_loss_amount) {
                        std::cout << "[止损] Session " << session_id 
                                  << " 亏损 $" << profit << "，自动停止" << std::endl;
                        stop_trading_session(session_id);
                        log_session_activity(session_id, "止损触发，自动停止");
                    }
                }
            }

            // 清理过期/异常会话
            cleanup_expired_sessions();

        } catch (const std::exception& e) {
            std::cerr << "Error in trading loop: " << e.what() << std::endl;
        }

        // 每轮间隔
        std::this_thread::sleep_for(std::chrono::milliseconds(trading_interval_ms_));
    }

    std::cout << "Trading loop stopped" << std::endl;
}

// 执行交易会话
void TradingEngineManager::execute_trading_session(TradingSession* session) {
    if (!session) return;
    
    try {
        std::cout << "Executing session: " << session->session_id << std::endl;
        
        // 执行套利策略
        if (session->arbitrage_strategy) {
            execute_arbitrage_session(session);
        }
        
        // 执行做市策略
        if (session->market_making_strategy) {
            execute_market_making_session(session);
        }
        
        // 更新最后活动时间
        session->last_update = std::chrono::system_clock::now();
        
    } catch (const std::exception& e) {
        std::cerr << "Error executing session " << session->session_id << ": " << e.what() << std::endl;
        session->status = EngineStatus::ERROR;
    }
}

// 执行套利会话
void TradingEngineManager::execute_arbitrage_session(TradingSession* session) {
    if (!session->arbitrage_strategy) return;
    
    std::cout << "Running arbitrage strategy for " << session->request.symbol << std::endl;
    
    // 调用套利策略的运行函数
    StrategyResult result = session->arbitrage_strategy->run_once();
    auto time_t = std::chrono::system_clock::to_time_t(session->last_update);
    std::ostringstream ts;
    ts << "[" << std::put_time(std::localtime(&time_t), "%H:%M:%S") << "] ";

    for (const auto& line : result.logs) {
        session->log.push_back(ts.str() + line);
    }

    update_session_stats(session, result.profit, result.trades);

    if (result.profit > 0 || result.trades > 0) {
        log_session_activity(session->session_id,
            "Arbitrage executed: profit=" + std::to_string(result.profit) +
            ", trades=" + std::to_string(result.trades));
    }

}

// 执行做市会话
void TradingEngineManager::execute_market_making_session(TradingSession* session) {
    if (!session->market_making_strategy) return;
    
    std::cout << "Running market making strategy for " << session->request.symbol 
              << " on " << session->request.exchange << std::endl;
    
    // 调用做市策略的运行函数
    StrategyResult result = session->market_making_strategy->run_once();
    auto time_t = std::chrono::system_clock::to_time_t(session->last_update);
    std::ostringstream ts;
    ts << "[" << std::put_time(std::localtime(&time_t), "%H:%M:%S") << "] ";

    for (const auto& line : result.logs) {
        session->log.push_back(ts.str() + line);
    }

    update_session_stats(session, result.profit, result.trades);

    if (result.profit > 0 || result.trades > 0) {
        log_session_activity(session->session_id,
            "Market making executed: profit=" + std::to_string(result.profit) +
            ", trades=" + std::to_string(result.trades));
    }
    
}

// 清理过期会话
void TradingEngineManager::cleanup_expired_sessions() {
    auto now = std::chrono::system_clock::now();
    std::vector<std::string> sessions_to_remove;
    
    for (const auto& [session_id, session] : trading_sessions_) {
        // 检查会话是否超时（比如1小时没有活动）
        auto inactive_duration = std::chrono::duration_cast<std::chrono::hours>(
            now - session->last_update).count();
        
        if (inactive_duration > 1 && session->status == EngineStatus::STOPPED) {
            sessions_to_remove.push_back(session_id);
        }
        
        // 检查错误状态的会话
        if (session->status == EngineStatus::ERROR) {
            sessions_to_remove.push_back(session_id);
        }
    }
    
    // 删除过期会话
    for (const auto& session_id : sessions_to_remove) {
        std::cout << "Cleaning up expired session: " << session_id << std::endl;
        remove_trading_session(session_id);
    }
}

std::string TradingEngineManager::generate_session_id() {
    static const char charset[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";

    std::default_random_engine rng(std::random_device{}());
    std::uniform_int_distribution<> dist(0, sizeof(charset) - 2);

    std::ostringstream oss;
    oss << "session_";
    for (int i = 0; i < 8; ++i)
        oss << charset[dist(rng)];

    return oss.str();
}
