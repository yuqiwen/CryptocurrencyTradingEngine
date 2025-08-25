
#include "engine_api.h"

EngineAPI::EngineAPI()
    : engine_("host=localhost port=15432 dbname=crypto_data user=postgres password=password", "127.0.0.1", 6379, "") {}

bool EngineAPI::initialize() {
    return engine_.initialize();
}

void EngineAPI::start_engine() {
    engine_.start_engine();
}

std::string EngineAPI::create_session(const ClientRequest& req) {
    return engine_.create_trading_session(req);
}

bool EngineAPI::start_session(const std::string& session_id) {
    return engine_.start_trading_session(session_id);
}

bool EngineAPI::stop_session(const std::string& session_id) {
    return engine_.stop_trading_session(session_id);
}

std::vector<std::string> EngineAPI::get_all_sessions() {
    return engine_.get_active_sessions();
}

TradingSession* EngineAPI::get_session(const std::string& session_id) {
    return engine_.get_session(session_id);
}

TradingEngineManager& EngineAPI::get_engine() {
    return engine_;
}
