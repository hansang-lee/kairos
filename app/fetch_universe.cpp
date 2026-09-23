#include <ctime>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "common/util.hpp"
#include "data/kis_provider.hpp"
#include "yfinance.hpp"
#include "portfolio/price_cache.hpp"

/**
 * Fills cache/daily/ for a universe and reports what each ticker actually has.
 *
 * Separate from `sweep`, which fetches as a side effect of running strategies.
 * Adding an asset class to a universe is its own step, and it needs an answer to
 * one question before any backtest is worth running: does this fund have enough
 * history to be in the comparison at all. A fund listed in 2020 put beside one
 * listed in 2015 does not diversify the earlier years; it shortens them.
 */
namespace {

std::string dayOf(int64_t ts) {
    const std::time_t t = static_cast<std::time_t>(ts);
    std::tm           tm{};
    gmtime_r(&t, &tm);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

void printUsage() {
    std::cout << "Usage:\n"
              << "  fetch_universe --universe <path> [--from YYYY-MM-DD] [--to YYYY-MM-DD]\n"
              << "                 [--source kis|yahoo] [--refetch]\n\n"
              << "  Fetches every ticker in the universe into cache/daily/ and prints the history\n"
              << "  each one turned out to have. A ticker already covering the range is left alone\n"
              << "  unless --refetch says otherwise.\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string universePath;
    std::string from    = "2015-01-01";
    std::string to      = "2026-09-22";
    bool        refetch = false;
    std::string source  = "kis";

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--universe" && i + 1 < argc)
            universePath = argv[++i];
        else if (arg == "--from" && i + 1 < argc)
            from = argv[++i];
        else if (arg == "--to" && i + 1 < argc)
            to = argv[++i];
        else if (arg == "--source" && i + 1 < argc)
            source = argv[++i];
        else if (arg == "--refetch")
            refetch = true;
        else {
            printUsage();
            return (arg == "--help" || arg == "-h") ? 0 : 1;
        }
    }
    if (universePath.empty()) {
        printUsage();
        return 1;
    }

    const auto universe = util::loadJsonConfig(universePath);
    if (!universe || !universe->contains("tickers")) {
        std::cerr << "[-] No tickers in " << universePath << std::endl;
        return 1;
    }

    // Two sources because the universes do not overlap: KIS serves KRX, and the US
    // funds a dollar-denominated universe needs are only on Yahoo.
    const bool  useYahoo = (source == "yahoo");
    KisProvider kis;
    if (useYahoo) {
        yFinance::init();
    }
    util::Defer cleanup([useYahoo] {
        if (useYahoo) {
            yFinance::close();
        }
    });
    const auto wanted = portfolio::parseDate(from);

    std::cout << std::left << std::setw(9) << "code" << std::setw(30) << "name" << std::right << std::setw(7) << "bars"
              << std::setw(13) << "from" << std::setw(13) << "to" << "  status\n"
              << std::string(84, '-') << "\n";

    std::size_t ok = 0, tooShort = 0, failed = 0;
    for (const auto& t : (*universe)["tickers"]) {
        const std::string code = t.value("code", "");
        const std::string name = t.value("name", code);
        if (code.empty()) {
            continue;
        }

        auto cached = refetch ? nullptr : portfolio::loadCachedDaily(code);
        // Both ends, not just the start. Checking only the start meant a cache that
        // stopped nine months ago was silently reused, and every backtest quietly
        // ended in December while claiming to run to today.
        const bool covers = cached && !cached->timestamps.empty()
                         && cached->timestamps.front() <= wanted + 7 * 86400
                         && cached->timestamps.back() >= portfolio::parseDate(to) - 10 * 86400;

        std::string status = "cached";
        auto        data   = covers ? cached : nullptr;
        if (!data) {
            data = useYahoo ? yFinance::getStockInfo(code, from, to, "1d") : kis.getStockInfo(code, from, to);
            if (data) {
                data->ticker = code;  // Yahoo echoes its own spelling; the cache key is ours
            }
            if (data && portfolio::saveCachedDaily(*data)) {
                status = "fetched";
            } else if (data) {
                // Not cached: a short fetch is either a young fund or a rate limit,
                // and writing it would make the two indistinguishable later.
                status = "NOT CACHED (too few bars)";
            } else {
                status = "FETCH FAILED";
            }
        }

        std::cout << std::left << std::setw(9) << code << std::setw(30) << name.substr(0, 29) << std::right;
        if (data && !data->timestamps.empty()) {
            std::cout << std::setw(7) << data->close.size() << std::setw(13) << dayOf(data->timestamps.front())
                      << std::setw(13) << dayOf(data->timestamps.back());
            if (data->close.size() >= 300) {
                ++ok;
            } else {
                ++tooShort;
            }
        } else {
            std::cout << std::setw(7) << "-" << std::setw(13) << "-" << std::setw(13) << "-";
            ++failed;
        }
        std::cout << "  " << status << "\n";
    }

    std::cout << "\n " << ok << " usable, " << tooShort << " too short, " << failed << " unavailable\n";
    return failed > 0 ? 1 : 0;
}
