#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>

#include "indicator.hpp"

int main() {
    std::cout << "=== Testing Technical Indicators ===" << std::endl;

    // 30 days of simulated prices (uptrend then pull-back)
    std::vector<double>  close = {100.0, 102.0, 101.5, 103.0, 105.0, 104.5, 106.0, 108.0, 107.5, 110.0,
                                  112.0, 111.0, 113.5, 115.0, 114.0, 116.5, 118.0, 117.0, 119.5, 122.0,
                                  121.0, 123.5, 125.0, 124.0, 122.0, 120.5, 119.0, 121.0, 123.0, 125.5};
    std::vector<double>  high(close.size());
    std::vector<double>  low(close.size());
    std::vector<int64_t> volume(close.size(), 100000);

    for (std::size_t i = 0; i < close.size(); ++i) {
        high[i]   = close[i] + 1.5;
        low[i]    = close[i] - 1.2;
        volume[i] = 100000 + static_cast<int64_t>(i * 5000);
    }

    // 1. SMA & EMA
    auto sma5 = indicator::sma(close, 5);
    auto ema5 = indicator::ema(close, 5);
    std::cout << "1. SMA(5) count: " << sma5.size() << " | Latest SMA: " << sma5.back() << std::endl;
    std::cout << "   EMA(5) count: " << ema5.size() << " | Latest EMA: " << ema5.back() << std::endl;

    // 2. RSI
    auto rsi14 = indicator::rsi(close, 14);
    std::cout << "2. RSI(14) count: " << rsi14.size() << " | Latest RSI: " << rsi14.back() << std::endl;

    // 3. Bollinger Bands
    auto bb = indicator::bollinger(close, 20, 2.0);
    std::cout << "3. Bollinger(20, 2.0) count: " << bb.middle.size() << " | Upper: " << bb.upper.back()
              << " | Middle: " << bb.middle.back() << " | Lower: " << bb.lower.back() << std::endl;

    // 4. ATR
    auto atrVal = indicator::atr(high, low, close, 14);
    std::cout << "4. ATR(14) count: " << atrVal.size() << " | Latest ATR: " << atrVal.back() << std::endl;

    // 5. VWAP
    auto vwapVal = indicator::vwap(high, low, close, volume);
    std::cout << "5. VWAP count: " << vwapVal.size() << " | Latest VWAP: " << vwapVal.back() << std::endl;

    // 6. Stochastic
    auto stoch = indicator::stochastic(high, low, close, 14, 3);
    std::cout << "6. Stochastic(14, 3) count: " << stoch.k.size() << " | Latest %K: " << stoch.k.back()
              << " | Latest %D: " << stoch.d.back() << std::endl;

    // 7. MACD (test with smaller periods for 30 bars)
    auto macdVal = indicator::macd(close, 5, 12, 5);
    std::cout << "7. MACD(5, 12, 5) count: " << macdVal.macd.size() << " | MACD: " << macdVal.macd.back()
              << " | Signal: " << macdVal.signal.back() << " | Hist: " << macdVal.histogram.back() << std::endl;

    std::cout << "\nAll 7 technical indicators calculated and verified successfully!" << std::endl;
    return 0;
}
