#pragma once
#include <string>
#include <vector>

struct StrategyResult {
    double profit = 0.0;
    int trades = 0;
    std::vector<std::string> logs;
};
