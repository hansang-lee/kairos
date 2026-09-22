#include "portfolio/price_cache.hpp"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include "common/util.hpp"

namespace portfolio {

std::shared_ptr<StockInfo> loadCachedDaily(const std::string& ticker) {
    auto data    = std::make_shared<StockInfo>();
    data->ticker = ticker;

    std::ifstream in(util::resolveFromExe("cache/daily/" + ticker + ".csv"));
    if (!in.is_open()) {
        return nullptr;
    }
    std::string line;
    std::getline(in, line);  // header
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string        cell;
        double             ohlc[4];
        try {
            if (!std::getline(ss, cell, ','))
                continue;
            const int64_t ts = std::stoll(cell);
            for (double& v : ohlc) {
                if (!std::getline(ss, cell, ','))
                    throw std::runtime_error("short row");
                v = std::stod(cell);
            }
            if (!std::getline(ss, cell, ','))
                continue;
            data->timestamps.push_back(ts);
            data->open.push_back(ohlc[0]);
            data->high.push_back(ohlc[1]);
            data->low.push_back(ohlc[2]);
            data->close.push_back(ohlc[3]);
            data->volume.push_back(std::stoll(cell));
        } catch (const std::exception&) {
            continue;
        }
    }
    return data->close.empty() ? nullptr : data;
}

bool saveCachedDaily(const StockInfo& data, std::size_t minBars) {
    if (data.ticker.empty() || data.close.size() < minBars || data.timestamps.size() != data.close.size()) {
        return false;
    }
    const std::string path = util::resolveFromExe("cache/daily/" + data.ticker + ".csv");

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out << "timestamp,open,high,low,close,volume\n" << std::fixed << std::setprecision(4);
    for (std::size_t i = 0; i < data.close.size(); ++i) {
        out << data.timestamps[i] << "," << data.open[i] << "," << data.high[i] << "," << data.low[i] << ","
            << data.close[i] << "," << data.volume[i] << "\n";
    }
    return true;
}

int64_t parseDate(const std::string& date) {
    std::tm tm = {};
    tm.tm_year = std::stoi(date.substr(0, 4)) - 1900;
    tm.tm_mon  = std::stoi(date.substr(5, 2)) - 1;
    tm.tm_mday = std::stoi(date.substr(8, 2));
    tm.tm_hour = 9;
    return timegm(&tm);
}

std::shared_ptr<StockInfo> sliceTo(const std::shared_ptr<StockInfo>& series, int64_t from, int64_t to) {
    auto out = std::make_shared<StockInfo>();
    if (!series) {
        return out;
    }
    out->ticker = series->ticker;
    for (std::size_t i = 0; i < series->timestamps.size(); ++i) {
        if (series->timestamps[i] < from || series->timestamps[i] >= to)
            continue;
        out->timestamps.push_back(series->timestamps[i]);
        out->open.push_back(series->open[i]);
        out->high.push_back(series->high[i]);
        out->low.push_back(series->low[i]);
        out->close.push_back(series->close[i]);
        out->volume.push_back(series->volume[i]);
    }
    return out;
}

UniverseLoad loadUniverse(const std::string& universePath, const std::string& startDate, const std::string& endDate,
                          double defaultExpenseRatio, std::size_t minBars) {
    UniverseLoad out;

    const auto universe = util::loadJsonConfig(universePath);
    if (!universe || !universe->contains("tickers")) {
        return out;
    }

    const int64_t from = parseDate(startDate);
    const int64_t to   = parseDate(endDate);

    for (const auto& t : (*universe)["tickers"]) {
        const std::string code = t.value("code", "");
        if (code.empty()) {
            continue;
        }
        auto cached = loadCachedDaily(code);
        if (!cached) {
            out.missing.push_back(code);
            continue;
        }
        auto window = sliceTo(cached, from, to);
        if (window->close.size() <= minBars) {
            out.missing.push_back(code);
            continue;
        }

        const bool stated = t.contains("expense_ratio") && !t["expense_ratio"].is_null();
        if (!stated) {
            out.allFeesKnown = false;
        }
        out.expenseRatios.push_back(stated ? t["expense_ratio"].get<double>() : defaultExpenseRatio);
        out.assetClasses.push_back(t.value("asset_class", std::string{}));
        out.series.push_back(window);
    }
    return out;
}

}  // namespace portfolio
