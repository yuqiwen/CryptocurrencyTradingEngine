
#pragma once
#include "trading_engine_manager.h"

class EngineAPI {
public:
    EngineAPI();

    bool initialize();
    void start_engine();
    std::string create_session(const ClientRequest& req);
    bool start_session(const std::string& session_id);
    bool stop_session(const std::string& session_id);
    std::vector<std::string> get_all_sessions();
    TradingSession* get_session(const std::string& session_id);
    TradingEngineManager& get_engine();  // 暴露底层引用

private:
    TradingEngineManager engine_;
};
