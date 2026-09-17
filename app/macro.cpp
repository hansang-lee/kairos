#include <cstdlib>
#include <iostream>
#include <string>

#include <unistd.h>

#include <nlohmann/json.hpp>

#include "common/util.hpp"
#include "macro_scorer.hpp"
#include "yfinance.hpp"

int main(int argc, char* argv[]) {
    const char* apiKey = std::getenv("FRED_API_KEY");
    if (!apiKey || std::string(apiKey).empty()) {
        std::cerr << "Error: FRED_API_KEY environment variable is not set.\n"
                  << "Get your free API key at: https://fred.stlouisfed.org/docs/api/api_key.html\n"
                  << "Usage: export FRED_API_KEY=<your_key>" << std::endl;
        return 1;
    }

    // Parse arguments
    bool        jsonMode = false;
    std::string configPath;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--json") {
            jsonMode = true;
        } else {
            configPath = arg;
        }
    }

    if (configPath.empty()) {
        configPath = util::resolveFromExe("config/macro_allocation.json");
    }

    yFinance::init();
    util::Defer _cleanup([] { yFinance::close(); });

    if (jsonMode) {
        auto result = MacroScorer::analyzeJson(apiKey, configPath);
        if (result.empty()) {
            std::cerr << "Analysis failed." << std::endl;
            return 1;
        }
        std::cout << result.dump(2) << std::endl;
    } else {
        if (!MacroScorer::analyze(apiKey, configPath)) {
            std::cerr << "Analysis failed." << std::endl;
            return 1;
        }
    }

    return 0;
}
