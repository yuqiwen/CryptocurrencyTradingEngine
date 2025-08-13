
#pragma once

#include <crow.h>
#include "engine_api.h"

inline void setup_routes(crow::SimpleApp& app, EngineAPI& engine_api) {
    CROW_ROUTE(app, "/create_session").methods("POST"_method)([&engine_api](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body) return crow::response(400, "Invalid JSON");

        ClientRequest r;
        r.client_id = body["client_id"].s();
        r.symbol = body["symbol"].s();
        r.exchange = body["exchange"].s();
        r.max_amount = body["max_amount"].d();
        r.target_profit = body["target_profit"].d();
        r.take_profit_ratio = body.has("take_profit_ratio") ? body["take_profit_ratio"].d() : 0.1;
        r.stop_loss_ratio = body.has("stop_loss_ratio") ? body["stop_loss_ratio"].d() : 0.05;
        std::string mode = body["mode"].s();

        if (mode == "ARBITRAGE") r.mode = TradingMode::ARBITRAGE;
        else if (mode == "MARKET_MAKING") r.mode = TradingMode::MARKET_MAKING;
        else r.mode = TradingMode::MIXED;

        auto session_id = engine_api.create_session(r);
        if (session_id.empty()) return crow::response(500, "Failed to create session");

        return crow::response(200, crow::json::wvalue({{"session_id", session_id}}));
    });

    CROW_ROUTE(app, "/start_session/<string>").methods("POST"_method)
    ([&engine_api](const crow::request&, const std::string& session_id) {
        if (!engine_api.start_session(session_id)) {
            return crow::response(500, "Failed to start session");
        }
        return crow::response(200, "Session started");
    });

    CROW_ROUTE(app, "/stop_session/<string>").methods("POST"_method)
    ([&engine_api](const crow::request&, const std::string& session_id) {
        if (!engine_api.stop_session(session_id)) {
            return crow::response(500, "Failed to stop session");
        }
        return crow::response(200, "Session stopped");
    });

    CROW_ROUTE(app, "/sessions").methods("GET"_method)
    ([&engine_api]() {
        auto sessions = engine_api.get_all_sessions();
        crow::json::wvalue result;
        std::vector<crow::json::wvalue> session_list;
        for (const auto& id : sessions) session_list.push_back(id);
        result["sessions"] = std::move(session_list);
        return crow::response(result);
    });

    CROW_ROUTE(app, "/session_log/<string>").methods("GET"_method)
    ([&engine_api](const std::string& session_id) {
        auto session = engine_api.get_session(session_id);
        if (!session) return crow::response(404, "Session not found");

        crow::json::wvalue result;
        result["log"] = crow::json::wvalue::list(session->log.begin(), session->log.end());
        return crow::response(result);
    });

}
