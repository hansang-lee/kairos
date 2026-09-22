#include "portfolio/portfolio_data.hpp"

#include <algorithm>
#include <map>

namespace portfolio {

PortfolioData PortfolioData::align(const std::vector<std::shared_ptr<StockInfo>>& series) {
    PortfolioData out;

    std::vector<std::map<int64_t, double>> byTs;
    for (const auto& s : series) {
        if (!s || s->close.empty() || s->timestamps.size() != s->close.size()) {
            continue;  // an asset with no usable history is simply not in the universe
        }
        std::map<int64_t, double> m;
        for (std::size_t i = 0; i < s->timestamps.size(); ++i) {
            m[s->timestamps[i]] = s->close[i];
        }
        out.tickers.push_back(s->ticker);
        byTs.push_back(std::move(m));
    }
    if (byTs.empty()) {
        return out;
    }

    // The union, so an asset closed for a local holiday does not remove that day
    // from every other asset as an intersection would.
    std::vector<int64_t> all;
    for (const auto& m : byTs) {
        for (const auto& [ts, _] : m) {
            all.push_back(ts);
        }
    }
    std::sort(all.begin(), all.end());
    all.erase(std::unique(all.begin(), all.end()), all.end());
    out.timestamps = std::move(all);

    out.close.assign(byTs.size(), std::vector<double>(out.timestamps.size(), 0.0));
    out.available.assign(byTs.size(), std::vector<bool>(out.timestamps.size(), false));

    for (std::size_t a = 0; a < byTs.size(); ++a) {
        const auto& m    = byTs[a];
        double      last = 0.0;
        for (std::size_t b = 0; b < out.timestamps.size(); ++b) {
            const auto it = m.upper_bound(out.timestamps[b]);
            if (it != m.begin()) {
                last = std::prev(it)->second;
            }
            out.close[a][b] = last;
            // Only from the asset's first real price onward: before that, `last` is
            // zero, and after a forward-fill it is a price that did exist.
            out.available[a][b] = last > 0.0;
        }
    }
    return out;
}

PortfolioData PortfolioData::slice(std::size_t first, std::size_t last) const {
    PortfolioData out;
    out.tickers = tickers;

    const std::size_t hi = std::min(last, barCount());
    if (first >= hi) {
        out.close.assign(tickers.size(), {});
        out.available.assign(tickers.size(), {});
        return out;
    }

    out.timestamps.assign(timestamps.begin() + static_cast<std::ptrdiff_t>(first),
                          timestamps.begin() + static_cast<std::ptrdiff_t>(hi));
    out.close.resize(close.size());
    out.available.resize(available.size());
    for (std::size_t a = 0; a < close.size(); ++a) {
        out.close[a].assign(close[a].begin() + static_cast<std::ptrdiff_t>(first),
                            close[a].begin() + static_cast<std::ptrdiff_t>(hi));
        out.available[a].assign(available[a].begin() + static_cast<std::ptrdiff_t>(first),
                                available[a].begin() + static_cast<std::ptrdiff_t>(hi));
    }
    return out;
}

}  // namespace portfolio
