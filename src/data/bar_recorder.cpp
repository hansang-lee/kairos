#include "data/bar_recorder.hpp"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

#include "common/kst_time.hpp"
#include "common/util.hpp"

namespace data {

namespace {

/** KST calendar date of a bar timestamp, "YYYY-MM-DD". */
struct Bar {
    double  open = 0.0, high = 0.0, low = 0.0, close = 0.0;
    int64_t volume = 0;
};

/** Read a day's file into a timestamp-keyed map, so a merge is just an insert. */
std::map<int64_t, Bar> readFile(const std::string& path) {
    std::map<int64_t, Bar> bars;
    std::ifstream          in(path);
    if (!in.is_open()) {
        return bars;
    }
    std::string line;
    std::getline(in, line);  // header
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string        cell;
        int64_t            ts = 0;
        Bar                b;
        try {
            if (!std::getline(ss, cell, ','))
                continue;
            ts = std::stoll(cell);
            if (!std::getline(ss, cell, ','))
                continue;
            b.open = std::stod(cell);
            if (!std::getline(ss, cell, ','))
                continue;
            b.high = std::stod(cell);
            if (!std::getline(ss, cell, ','))
                continue;
            b.low = std::stod(cell);
            if (!std::getline(ss, cell, ','))
                continue;
            b.close = std::stod(cell);
            if (std::getline(ss, cell, ','))
                b.volume = std::stoll(cell);
        } catch (const std::exception&) {
            continue;  // a torn line from a killed process; skip it, keep the rest
        }
        bars[ts] = b;
    }
    return bars;
}

bool writeFile(const std::string& path, const std::map<int64_t, Bar>& bars) {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out << "timestamp,open,high,low,close,volume\n";
    out << std::fixed << std::setprecision(4);
    for (const auto& [ts, b] : bars) {
        out << ts << "," << b.open << "," << b.high << "," << b.low << "," << b.close << "," << b.volume << "\n";
    }
    return out.good();
}

}  // namespace

BarRecorder::BarRecorder(const std::string& rootDir)
    : root_(rootDir.empty() ? util::resolveFromExe("data/bars") : rootDir) {
    std::error_code ec;
    std::filesystem::create_directories(root_, ec);
}

std::string BarRecorder::pathFor(const std::string& ticker, const std::string& date) const {
    return root_ + "/" + ticker + "/" + date + ".csv";
}

std::string BarRecorder::pathFor(const std::string& ticker, const std::string& interval,
                                 const std::string& date) const {
    return root_ + "/" + ticker + "/" + interval + "/" + date + ".csv";
}

int BarRecorder::record(const std::string& ticker, const StockInfo& bars, const std::string& interval) {
    const std::size_t n = bars.close.size();
    if (n == 0 || bars.timestamps.size() != n) {
        return 0;
    }

    std::error_code ec;
    std::filesystem::create_directories(root_ + "/" + ticker + "/" + interval, ec);

    // A poll can straddle midnight only in theory, but grouping by date costs
    // nothing and keeps each file to one session.
    std::map<std::string, std::map<int64_t, Bar>> byDate;
    for (std::size_t i = 0; i < n; ++i) {
        Bar b;
        b.open   = i < bars.open.size() ? bars.open[i] : bars.close[i];
        b.high   = i < bars.high.size() ? bars.high[i] : bars.close[i];
        b.low    = i < bars.low.size() ? bars.low[i] : bars.close[i];
        b.close  = bars.close[i];
        b.volume = i < bars.volume.size() ? bars.volume[i] : 0;
        byDate[util::kstDateOf(bars.timestamps[i])][bars.timestamps[i]] = b;
    }

    int added = 0;
    for (const auto& [date, incoming] : byDate) {
        const std::string path     = pathFor(ticker, interval, date);
        auto              existing = readFile(path);
        const std::size_t before   = existing.size();
        for (const auto& [ts, b] : incoming) {
            existing[ts] = b;  // a re-fetched bar may have been revised; take the newer one
        }
        if (existing.size() == before) {
            continue;  // nothing new — the common case when polling faster than the bar interval
        }
        if (!writeFile(path, existing)) {
            return -1;
        }
        added += static_cast<int>(existing.size() - before);
    }
    return added;
}

int BarRecorder::recordSeries(const std::string& ticker, const std::string& series, const StockInfo& bars) {
    const std::size_t n = bars.close.size();
    if (n == 0 || bars.timestamps.size() != n) {
        return 0;
    }

    std::error_code ec;
    std::filesystem::create_directories(root_ + "/" + ticker, ec);

    const std::string path     = pathFor(ticker, series);
    auto              existing = readFile(path);
    const std::size_t before   = existing.size();

    for (std::size_t i = 0; i < n; ++i) {
        Bar b;
        b.open                       = i < bars.open.size() ? bars.open[i] : bars.close[i];
        b.high                       = i < bars.high.size() ? bars.high[i] : bars.close[i];
        b.low                        = i < bars.low.size() ? bars.low[i] : bars.close[i];
        b.close                      = bars.close[i];
        b.volume                     = i < bars.volume.size() ? bars.volume[i] : 0;
        existing[bars.timestamps[i]] = b;
    }

    if (existing.size() == before) {
        return 0;
    }
    if (!writeFile(path, existing)) {
        return -1;
    }
    return static_cast<int>(existing.size() - before);
}

std::shared_ptr<StockInfo> BarRecorder::loadSeries(const std::string& ticker, const std::string& series) const {
    const auto bars = readFile(pathFor(ticker, series));
    if (bars.empty()) {
        return nullptr;
    }

    auto result      = std::make_shared<StockInfo>();
    result->ticker   = ticker;
    result->currency = "KRW";
    for (const auto& [ts, b] : bars) {
        result->timestamps.push_back(ts);
        result->open.push_back(b.open);
        result->high.push_back(b.high);
        result->low.push_back(b.low);
        result->close.push_back(b.close);
        result->volume.push_back(b.volume);
    }
    return result;
}

std::vector<std::string> BarRecorder::storedDates(const std::string& ticker, const std::string& interval) const {
    std::vector<std::string> dates;
    std::error_code          ec;
    const std::string        dir = root_ + "/" + ticker + "/" + interval;
    if (!std::filesystem::exists(dir, ec)) {
        return dates;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.path().extension() != ".csv") {
            continue;
        }
        // Named series (e.g. "daily.csv") live alongside the per-day files and are
        // not dates, so a range query must not try to compare against them.
        const std::string stem = entry.path().stem().string();
        if (stem.size() == 10 && stem[4] == '-' && stem[7] == '-') {
            dates.push_back(stem);
        }
    }
    std::sort(dates.begin(), dates.end());
    return dates;
}

std::shared_ptr<StockInfo> BarRecorder::load(const std::string& ticker, const std::string& startDate,
                                             const std::string& endDate, const std::string& interval) const {
    auto result      = std::make_shared<StockInfo>();
    result->ticker   = ticker;
    result->currency = "KRW";

    // Merging through one map removes any overlap between files and guarantees the
    // strictly increasing timestamps the backtest engine assumes.
    std::map<int64_t, Bar> all;
    for (const auto& date : storedDates(ticker, interval)) {
        if (date < startDate || date > endDate) {
            continue;
        }
        for (const auto& [ts, b] : readFile(pathFor(ticker, interval, date))) {
            all[ts] = b;
        }
    }
    if (all.empty()) {
        return nullptr;
    }

    result->timestamps.reserve(all.size());
    for (const auto& [ts, b] : all) {
        result->timestamps.push_back(ts);
        result->open.push_back(b.open);
        result->high.push_back(b.high);
        result->low.push_back(b.low);
        result->close.push_back(b.close);
        result->volume.push_back(b.volume);
    }
    return result;
}

}  // namespace data
