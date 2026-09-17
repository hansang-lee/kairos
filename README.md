# libyfinance

> C++17 기반 퀀트 자동 거래 시스템 — 한국/미국 주식 시장 대상

[![Daily Macro Report](https://github.com/hslee/libyfinance/actions/workflows/macro-report.yml/badge.svg)](https://github.com/hslee/libyfinance/actions)

---

## 📋 개요

libyfinance는 C++17로 구현된 고성능 퀀트 투자 프레임워크입니다.

**핵심 기능:**
- **24가지 기술 지표** — 추세(SMA/EMA/WMA/ADX/Parabolic SAR/SuperTrend/Aroon), 모멘텀(RSI/MACD/ROC/CCI/Williams %R/TRIX/Stochastic), 거래량(VWAP/OBV/MFI/CMF/A·D Line), 변동성(Bollinger/ATR/StdDev/Keltner/Donchian)
- **15가지 트레이딩 전략** — 카테고리(스윙/추세추종/포지션)별로 분류 (아래 [전략 카테고리](#-전략-카테고리) 참고)
- **백테스트 엔진** — 수수료/슬리피지 반영, 종합 스코어(0~100) 산출
- **매크로 분석** — FRED 12개 경제 지표 + CNN Fear & Greed → 4국면 판정
- **한투 OpenAPI 연동** — 국장 시세 데이터 수집 + 모의투자 주문/잔고 조회 (KIS REST API)
- **동적 전략 관리** — JSON 기반 포트폴리오 설정으로 전략 동적 로딩
- **자동 대시보드** — GitHub Actions 기반 일일 매크로 리포트

---

## 🏛️ 아키텍처

```
libyfinance/
├── include/                     # C++ 헤더
│   ├── yfinance.hpp             # Yahoo Finance / FRED / CNN F&G API 클라이언트
│   ├── indicator.hpp            # 기술 지표 24종 (추세/모멘텀/거래량/변동성)
│   ├── stock_info.hpp           # StockInfo 구조체 (OHLCV 시계열)
│   ├── fng_info.hpp             # FearAndGreedInfo 구조체
│   ├── fred_info.hpp            # FredSeriesInfo 구조체
│   ├── macro_scorer.hpp         # 5축 매크로 스코어 + 4국면 판정
│   ├── strategy/
│   │   ├── istrategy.hpp        # IStrategy 순수 가상 인터페이스
│   │   └── strategy_factory.hpp # JSON 기반 전략 팩토리 (category 필드 포함)
│   ├── backtest/
│   │   └── backtest_engine.hpp  # 단일종목 백테스트 엔진
│   ├── macro/
│   │   └── macro_backtester.hpp # 매크로 포트폴리오 백테스트
│   ├── broker/
│   │   ├── kis_auth.hpp         # 한투 OpenAPI OAuth2 인증
│   │   └── kis_trader.hpp       # 한투 모의/실전 주문·잔고조회
│   └── data/
│       ├── idata_provider.hpp   # 데이터 소스 인터페이스
│       └── kis_provider.hpp     # 한투 시세 데이터 수집
│
├── src/                         # C++ 구현체
├── lib/                         # 전략 구현체 (15개, 전략별 hpp/cpp 디렉토리)
│   │                             # 스윙: rsi, bollinger, stochastic_reversal, williams_r, cci_reversal, mfi_reversal
│   │                             # 추세추종: sma_crossover, macd, adx_trend, supertrend_follow, aroon_trend, psar_trend
│   │                             # 포지션: donchian_breakout, obv_trend, keltner_breakout
│
├── app/                         # CLI 실행 파일 (14개)
├── config/                      # JSON 설정 파일
│   ├── portfolio.json           # 전략 프로필 (동적 로딩)
│   ├── macro_allocation.json    # 매크로 배분 설정
│   └── strategies/              # 매크로 전략 프로파일 (aggressive/balanced/defensive)
│
├── docs/                        # API 문서 + GitHub Pages 대시보드
└── .github/workflows/           # CI/CD (매크로 일일 리포트)
```

---

## 🚀 빌드

### 사전 요구사항

```bash
sudo apt install cmake ninja-build libcurl4-openssl-dev nlohmann-json3-dev
```

### 빌드 실행

```bash
# Release 빌드 (기본)
./make.sh

# Debug 빌드
./make.sh Debug
```

빌드 산출물: `build/Release/` (또는 `build/Debug/`)

### Docker 빌드

```bash
./docker.sh build    # 이미지 빌드
./docker.sh run      # 컨테이너 실행
```

---

## 📖 사용법

### 미국 주식 조회

```bash
./build/Release/app/stock AAPL 1d 1y
```

### 한국 주식 조회 (KIS API)

```bash
# .env 파일에 KIS 크레덴셜 설정 필요 (.env.example 참조)
./build/Release/app/kis_stock 005930 2024-01-01 2024-12-31
```

### 모의투자 주문 / 잔고조회 (KIS API)

```bash
# .env에 KIS_PAPER_* 크레덴셜 설정 필요 (기본은 모의투자 모드)
./build/Release/app/kis_order balance
./build/Release/app/kis_order buy  005930 1        # 시장가 매수 1주
./build/Release/app/kis_order sell 005930 1 75000  # 75,000원 지정가 매도 1주
```

### 백테스트

```bash
# SMA Crossover 전략으로 AAPL 1년 백테스트
./build/Release/app/backtest AAPL sma 1y
```

### 전략 스윕 (멀티 전략 × 멀티 종목 비교)

```bash
./build/Release/app/strategy_sweep
```

### 동적 전략 실행 (포트폴리오 기반)

```bash
# 전략 목록 조회
./build/Release/app/run_strategy --list

# 특정 전략 실행 (ID 기반)
./build/Release/app/run_strategy --id 1

# 전체 전략 실행
./build/Release/app/run_strategy --all

# 커스텀 설정 파일 사용
./build/Release/app/run_strategy --config my_portfolio.json --all
```

### 매크로 경제 분석

```bash
# FRED API 키 필요
export FRED_API_KEY=your_key
./build/Release/app/macro config/macro_allocation.json
```

### Fear & Greed Index

```bash
./build/Release/app/fng
```

---

## ⚙️ 설정

### `.env` — 환경변수 (gitignored)

`.env.example`을 복사하여 `.env`를 생성하고, 실제 키 값을 입력하세요.

```bash
cp .env.example .env
```

### `config/portfolio.json` — 전략 프로필

JSON으로 전략을 정의하면 코드 수정 없이 전략 추가/변경이 가능합니다:

```json
{
  "strategies": [
    {
      "id": 1,
      "name": "Samsung RSI",
      "ticker": "005930",
      "market": "KRX",
      "type": "rsi",
      "category": "swing",
      "params": { "period": 14, "oversold": 30.0, "overbought": 70.0 },
      "position_pct": 0.5,
      "stop_loss_pct": 3.0
    }
  ]
}
```

**지원 전략 타입:** `sma_crossover`, `rsi`, `macd`, `bollinger`, `stochastic_reversal`, `williams_r`, `cci_reversal`, `mfi_reversal`, `adx_trend`, `supertrend`, `aroon_trend`, `psar_trend`, `donchian_breakout`, `obv_trend`, `keltner_breakout` (`params`의 기본값은 `src/strategy/strategy_factory.cpp` 참고)

---

## 🗂️ 전략 카테고리

일봉(OHLCV) 데이터 + 하루 1회 실행(cron/수동) 구조로는 틱단타·초단타·분단위 데이지트레이딩을 실제로 검증할 수 없어(오더북/분봉 데이터가 없음) 이번 확장에서는 제외했고, 대신 일봉으로 실제 작동·검증 가능한 3개 카테고리로 정리했습니다. 틱단타를 실제로 하려면 KIS 분봉 API + 상시 실행 프로세스가 별도로 필요합니다.

| 카테고리 | 보유 기간 | 성격 | 전략 |
|---|---|---|---|
| **swing** (스윙) | 며칠~1~2주 | 평균회귀 (과매수/과매도 반전) | rsi, bollinger, stochastic_reversal, williams_r, cci_reversal, mfi_reversal |
| **trend** (추세추종) | 1주~수주 | 추세 방향 추종 | sma_crossover, macd, adx_trend, supertrend, aroon_trend, psar_trend |
| **position** (포지션) | 수주~수개월 | 변동성 돌파 / 거래량 확인 | donchian_breakout, obv_trend, keltner_breakout |

`config/portfolio.json`의 각 전략 프로필에 `category` 필드로 태깅되어 있고, `run_strategy --list`에서 바로 확인할 수 있습니다.

---

## 📊 기술 지표

전부 일봉 OHLCV만으로 계산됩니다 (오더북/틱 데이터 불필요). `app/test_indicators.cpp`에서 24종 전체를 스모크 테스트합니다.

| 분류 | 지표 | 함수 |
|---|---|---|
| 추세 | SMA, EMA, WMA | `sma`/`ema`/`wma(prices, window)` |
| 추세 | ADX/DMI | `adx(high, low, close, period)` → `{plusDI, minusDI, adx}` |
| 추세 | Parabolic SAR | `parabolicSar(high, low, afStep, afMax)` |
| 추세 | SuperTrend | `superTrend(high, low, close, period, multiplier)` |
| 추세 | Aroon | `aroon(high, low, period)` → `{up, down}` |
| 모멘텀 | RSI | `rsi(prices, period)` |
| 모멘텀 | MACD | `macd(prices, fast, slow, signal)` |
| 모멘텀 | Stochastic | `stochastic(high, low, close, k, d)` |
| 모멘텀 | ROC | `roc(prices, period)` |
| 모멘텀 | CCI | `cci(high, low, close, period)` |
| 모멘텀 | Williams %R | `williamsR(high, low, close, period)` |
| 모멘텀 | TRIX | `trix(prices, period)` |
| 거래량 | VWAP | `vwap(high, low, close, volume)` |
| 거래량 | OBV | `obv(close, volume)` |
| 거래량 | MFI | `mfi(high, low, close, volume, period)` |
| 거래량 | CMF | `cmf(high, low, close, volume, period)` |
| 거래량 | A/D Line | `adLine(high, low, close, volume)` |
| 변동성 | Bollinger Bands | `bollinger(prices, period, stddev)` → `{upper, middle, lower}` |
| 변동성 | ATR | `atr(high, low, close, period)` |
| 변동성 | Rolling StdDev | `stddev(prices, window)` |
| 변동성 | Keltner Channels | `keltner(high, low, close, emaPeriod, atrPeriod, multiplier)` |
| 변동성 | Donchian Channels | `donchian(high, low, period)` |

모두 `namespace indicator` (`include/indicator.hpp`), 헤더 온리.

---

## 🧩 전략 개발 가이드

새로운 전략을 추가하려면:

### 1. 전략 파일 생성

```
lib/my_strategy/
├── my_strategy.hpp
└── my_strategy.cpp
```

### 2. IStrategy 인터페이스 구현

```cpp
#pragma once
#include "strategy/istrategy.hpp"
#include "indicator.hpp"

class MyStrategy : public IStrategy {
public:
    MyStrategy(/* params */);
    [[nodiscard]] std::string name() const override;
    void init(const StockInfo& data) override;
    [[nodiscard]] std::size_t warmupPeriod() const override;
    [[nodiscard]] Signal evaluate(const StockInfo& data, std::size_t index) override;
private:
    // 캐시된 지표 값
};
```

### 3. CMakeLists.txt 등록

루트 `CMakeLists.txt`:
```cmake
add_library(${PROJECT_NAME} SHARED
  # ... 기존 소스 ...
  lib/my_strategy/my_strategy.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE
  # ... 기존 경로 ...
  ${CMAKE_CURRENT_SOURCE_DIR}/lib/my_strategy
)
```

`app/CMakeLists.txt`의 `BUILD_APP` 매크로 내:
```cmake
target_include_directories(${APP} PRIVATE
  # ... 기존 경로 ...
  ${CMAKE_SOURCE_DIR}/lib/my_strategy
)
```

### 4. StrategyFactory에 등록

`src/strategy/strategy_factory.cpp`에 분기 추가:
```cpp
if (type == "my_strategy") {
    return std::make_unique<MyStrategy>(/* params from JSON */);
}
```

### 5. portfolio.json에 프로필 추가

```json
{
  "id": 6,
  "type": "my_strategy",
  "ticker": "AAPL",
  "params": { /* strategy-specific */ }
}
```

---

## 📈 CLI 앱 목록

| 앱 | 설명 |
|----|------|
| `stock` | Yahoo Finance 주식 데이터 조회 |
| `kis_stock` | 한투 API 국장 주식 데이터 조회 |
| `kis_order` | 한투 모의/실전 주문(매수/매도) 및 잔고조회 |
| `fng` | CNN Fear & Greed Index 조회 |
| `fred` | FRED 경제 지표 조회 |
| `backtest` | 단일 전략 백테스트 |
| `buy_and_hold` | 바이앤홀드 벤치마크 |
| `macro` | 매크로 경제 분석 (5축 점수 + 4국면) |
| `macro_backtest` | 매크로 기반 포트폴리오 백테스트 |
| `macro_sweep` | 매크로 전략 프로파일 비교 |
| `qld_dca_backtest` | QLD 적립식 투자 백테스트 |
| `strategy_sweep` | 4전략 × 4종목 멀티 스윕 |
| `run_strategy` | JSON 포트폴리오 기반 동적 전략 실행 |
| `test_indicators` | 기술 지표 검증 |

---

## 🗺️ 개발 로드맵

| Phase | 내용 | 상태 |
|-------|------|------|
| **1-A** | GTest 테스트 프레임워크 | 🔲 미구현 |
| **1-B** | 백테스트 현실성 (수수료/슬리피지) | ✅ 완료 |
| **1-C** | 추가 기술 지표 (MACD, BB, ATR, ...) | ✅ 완료 |
| **1-D** | 한국 시장 데이터 수집 (KIS API) | ✅ 완료 |
| **2-A** | 브로커 추상화 (IBroker) | 🔲 미구현 |
| **2-B** | 주문 관리 (OrderManager, RiskManager) | 🔲 미구현 |
| **2-C** | 자동매매 데몬 (trader) | 🔲 미구현 |
| **3** | 대시보드 & 알림 (Go 서버, Telegram) | 🔲 미구현 |
| **4** | AI/적응형 전략 (Python ML) | 🔲 미구현 |
| **5** | 상용화 (멀티테넌트) | 🔲 미구현 |

> 상세 기획: [docs/PLAN.md](docs/PLAN.md)

---

## 📁 주요 데이터 흐름

```
Yahoo Finance ─┐
FRED API ──────┤                    ┌─── BacktestEngine ──→ BacktestResult
CNN F&G ───────┼→ StockInfo/Macro  ─┤
KIS OpenAPI ───┘                    └─── StrategyFactory ──→ run_strategy
```

---

## 🔧 개발 환경

| 항목 | 값 |
|------|------|
| **C++ 표준** | C++17 |
| **빌드** | CMake 3.16+ / Ninja |
| **의존성** | libcurl, nlohmann-json |
| **포맷터** | clang-format (120자, 4칸 들여쓰기) |
| **CI/CD** | GitHub Actions |

---

## 📄 라이선스

Private project.
