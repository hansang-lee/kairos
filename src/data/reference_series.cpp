#include "data/reference_series.hpp"

#include <fstream>
#include <sstream>

#include "common/util.hpp"

namespace data {

ReferenceSeries::ReferenceSeries(const std::string& ticker, const std::string& cacheDir) {
    const std::string dir  = cacheDir.empty() ? util::resolveFromExe("cache/daily") : cacheDir;
    const std::string path = dir + "/" + ticker + ".csv";

    std::ifstream in(path);
    if (!in.is_open()) {
        return;
    }
    std::string line;
    std::getline(in, line);  // header
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string        cell;
        try {
            if (!std::getline(ss, cell, ','))
                continue;
            const int64_t ts = std::stoll(cell);
            // timestamp,open,high,low,close,volume — the close is the fifth field.
            for (int skip = 0; skip < 3; ++skip) {
                if (!std::getline(ss, cell, ','))
                    throw std::runtime_error("short row");
            }
            if (!std::getline(ss, cell, ','))
                continue;
            byTimestamp_[ts] = std::stod(cell);
        } catch (const std::exception&) {
            continue;  // a torn row must not discard the rest
        }
    }
}

double ReferenceSeries::closeAtOrBefore(int64_t ts) const {
    auto it = byTimestamp_.upper_bound(ts);
    if (it == byTimestamp_.begin()) {
        return 0.0;
    }
    return std::prev(it)->second;
}

}  // namespace data
