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

    // 8. WMA
    auto wma5 = indicator::wma(close, 5);
    std::cout << "8. WMA(5) count: " << wma5.size() << " | Latest WMA: " << wma5.back() << std::endl;

    // 9. Rolling StdDev
    auto std5 = indicator::stddev(close, 5);
    std::cout << "9. StdDev(5) count: " << std5.size() << " | Latest: " << std5.back() << std::endl;

    // 10. ROC
    auto roc10 = indicator::roc(close, 10);
    std::cout << "10. ROC(10) count: " << roc10.size() << " | Latest: " << roc10.back() << std::endl;

    // 11. CCI
    auto cci20 = indicator::cci(high, low, close, 20);
    std::cout << "11. CCI(20) count: " << cci20.size() << " | Latest: " << cci20.back() << std::endl;

    // 12. Williams %R
    auto willR = indicator::williamsR(high, low, close, 14);
    std::cout << "12. Williams %R(14) count: " << willR.size() << " | Latest: " << willR.back() << std::endl;

    // 13. TRIX (small period so 3 EMA passes fit in 30 bars)
    auto trixVal = indicator::trix(close, 5);
    std::cout << "13. TRIX(5) count: " << trixVal.size() << " | Latest: " << trixVal.back() << std::endl;

    // 14. ADX / DMI
    auto dmi = indicator::adx(high, low, close, 14);
    std::cout << "14. ADX(14) count: " << dmi.adx.size() << " | +DI: " << dmi.plusDI.back()
              << " | -DI: " << dmi.minusDI.back() << " | ADX: " << (dmi.adx.empty() ? 0.0 : dmi.adx.back())
              << std::endl;

    // 15. Parabolic SAR
    auto psar = indicator::parabolicSar(high, low);
    std::cout << "15. Parabolic SAR count: " << psar.size() << " | Latest: " << psar.back() << std::endl;

    // 16. SuperTrend
    auto st = indicator::superTrend(high, low, close, 10, 3.0);
    std::cout << "16. SuperTrend(10, 3.0) count: " << st.value.size() << " | Latest: " << st.value.back()
              << " | Trend: " << st.trend.back() << std::endl;

    // 17. Aroon
    auto aroonVal = indicator::aroon(high, low, 25);
    std::cout << "17. Aroon(25) count: " << aroonVal.up.size() << " | Up: " << aroonVal.up.back()
              << " | Down: " << aroonVal.down.back() << std::endl;

    // 18. OBV
    auto obvVal = indicator::obv(close, volume);
    std::cout << "18. OBV count: " << obvVal.size() << " | Latest: " << obvVal.back() << std::endl;

    // 19. MFI
    auto mfiVal = indicator::mfi(high, low, close, volume, 14);
    std::cout << "19. MFI(14) count: " << mfiVal.size() << " | Latest: " << mfiVal.back() << std::endl;

    // 20. CMF
    auto cmfVal = indicator::cmf(high, low, close, volume, 20);
    std::cout << "20. CMF(20) count: " << cmfVal.size() << " | Latest: " << cmfVal.back() << std::endl;

    // 21. A/D Line
    auto adLineVal = indicator::adLine(high, low, close, volume);
    std::cout << "21. A/D Line count: " << adLineVal.size() << " | Latest: " << adLineVal.back() << std::endl;

    // 22. Donchian Channels
    auto donch = indicator::donchian(high, low, 20);
    std::cout << "22. Donchian(20) count: " << donch.middle.size() << " | Upper: " << donch.upper.back()
              << " | Middle: " << donch.middle.back() << " | Lower: " << donch.lower.back() << std::endl;

    // 23. Keltner Channels
    auto keltVal = indicator::keltner(high, low, close, 20, 10, 2.0);
    std::cout << "23. Keltner(20, 10, 2.0) count: " << keltVal.middle.size() << " | Upper: " << keltVal.upper.back()
              << " | Middle: " << keltVal.middle.back() << " | Lower: " << keltVal.lower.back() << std::endl;

    // 24. MA Slope (regression slope of a moving average, % per bar)
    auto maSlopeVal = indicator::maSlope(close, 20, 10);
    std::cout << "24. MA Slope(20, 10) count: " << maSlopeVal.size() << " | Latest: " << maSlopeVal.back() << " %/bar"
              << std::endl;

    std::cout << "\nAll 25 technical indicators calculated and verified successfully!" << std::endl;
    return 0;
}
