
#include "engine_api.h"
#include "crow_router.h"
#include <crow.h>

int main() {
    EngineAPI engine_api;

    if (!engine_api.initialize()) {
        std::cerr << "[FATAL] EngineAPI 初始化失败" << std::endl;
        return 1;
    }

    engine_api.start_engine();

    crow::SimpleApp app;
    setup_routes(app, engine_api);

    std::cout << "Engine HTTP Server running on port 18080..." << std::endl;
    app.port(18080).multithreaded().run();

    return 0;
}
