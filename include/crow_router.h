#pragma once

#include <crow.h>
#include "engine_api.h"

void setup_routes(crow::SimpleApp& app, EngineAPI& engine_api);
