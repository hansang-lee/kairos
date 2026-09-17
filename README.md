# libyfinance

> C++17 기반 퀀트 자동 거래 시스템 — 한국/미국 주식 시장 대상

[![Daily Macro Report](https://github.com/hslee/libyfinance/actions/workflows/macro-report.yml/badge.svg)](https://github.com/hslee/libyfinance/actions)

---

## 📋 개요

libyfinance는 C++17로 구현된 고성능 퀀트 투자 프레임워크입니다.

**핵심 기능:**
- **7가지 기술 지표** — SMA, EMA, RSI, MACD, Bollinger Bands, ATR, VWAP, Stochastic
- **4가지 트레이딩 전략** — SMA Crossover, RSI, MACD, Bollinger Bands
- **백테스트 엔진** — 수수료/슬리피지 반영, 종합 스코어(0~100) 산출
- **매크로 분석** — FRED 12개 경제 지표 + CNN Fear & Greed → 4국면 판정
- **한투 OpenAPI 연동** — 국장 시세 데이터 수집 (KIS REST API)
- **동적 전략 관리** — JSON 기반 포트폴리오 설정으로 전략 동적 로딩
- **자동 대시보드** — GitHub Actions 기반 일일 매크로 리포트

---

## 🏛️ 아키텍처

```
libyfinance/
├── include/                     # C++ 헤더
│   ├── yfinance.hpp             # Yahoo Finance / FRED / CNN F&G API 클라이언트
│   ├── indicator.hpp            # 기술 지표 (SMA, EMA, RSI, MACD, BB, ATR, VWAP, Stochastic)
│   ├── stock_info.hpp           # StockInfo 구조체 (OHLCV 시계열)
│   ├── fng_info.hpp             # FearAndGreedInfo 구조체
│   ├── fred_info.hpp            # FredSeriesInfo 구조체
│   ├── macro_scorer.hpp         # 5축 매크로 스코어 + 4국면 판정
│   ├── strategy/
│   │   ├── istrategy.hpp        # IStrategy 순수 가상 인터페이스
│   │   └── strategy_factory.hpp # JSON 기반 전략 팩토리
│   ├── backtest/
│   │   └── backtest_engine.hpp  # 단일종목 백테스트 엔진
│   ├── macro/
│   │   └── macro_backtester.hpp # 매크로 포트폴리오 백테스트
│   ├── broker/
│   │   └── kis_auth.hpp         # 한투 OpenAPI OAuth2 인증
│   └── data/
│       ├── idata_provider.hpp   # 데이터 소스 인터페이스
│       └── kis_provider.hpp     # 한투 시세 데이터 수집
│
├── src/                         # C++ 구현체
├── lib/                         # 전략 구현체
│   ├── sma_crossover/           # SMA(20/50) 골든/데드 크로스
│   ├── rsi/                     # RSI(14, 30/70) 과매수/과매도
│   ├── macd/                    # MACD(12/26/9) 시그널 크로스
│   └── bollinger/               # Bollinger Bands(20, 2σ) 밴드 이탈
│
├── app/                         # CLI 실행 파일 (13개)
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
      "params": { "period": 14, "oversold": 30.0, "overbought": 70.0 },
      "position_pct": 0.5,
      "stop_loss_pct": 3.0
    }
  ]
}
```

**지원 전략 타입:** `sma_crossover`, `rsi`, `macd`, `bollinger`

---

## 📊 기술 지표

| 지표 | 함수 | 용도 |
|------|------|------|
| SMA | `indicator::sma(prices, window)` | 단순 이동평균 |
| EMA | `indicator::ema(prices, window)` | 지수 이동평균 |
| RSI | `indicator::rsi(prices, period)` | 과매수/과매도 판단 |
| MACD | `indicator::macd(prices, fast, slow, signal)` | 추세 전환 포착 |
| Bollinger Bands | `indicator::bollinger(prices, period, stddev)` | 변동성 밴드 |
| ATR | `indicator::atr(high, low, close, period)` | 변동성 측정 |
| VWAP | `indicator::vwap(high, low, close, volume)` | 기관 매매 기준가 |
| Stochastic | `indicator::stochastic(high, low, close, k, d)` | 과매수/과매도 |

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
