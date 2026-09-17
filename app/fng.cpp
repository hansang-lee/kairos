#include <iomanip>
#include <iostream>

#include "common/util.hpp"
#include "yfinance.hpp"


void printHeader() {
    // clang-format off
    std::clog << std::left
        << std::setw(20) << "(Date)"
        << std::setw(9) << "(Score)"
        << std::setw(15) << "(Rating)"
        << "\n-"
        << std::endl;
    // clang-format on
}

int main() {
    yFinance::init();
    util::Defer _cleanup([] { yFinance::close(); });

    const auto data = yFinance::getFearAndGreedIndex();
    if (!data) {
        return 1;
    }

    printHeader();

    for (std::size_t i = 0; i < data->timestamps.size(); i++) {
        // clang-format off
        std::clog << std::left
            << std::setw(20) << util::formatTime(data->timestamps[i])
            << std::fixed << std::setprecision(2)
            << std::setw(9) << data->scores[i]
            << std::setw(15) << data->ratings[i]
            << std::endl;
        // clang-format on
    }

    return 0;
}
