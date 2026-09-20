# 첫 실주문 검증 절차

**지금까지 성공한 주문은 0건입니다.** 이 저장소의 모든 것은 "주문을 보내기 직전"까지만
검증돼 있습니다. 주문 경로는 dry-run으로만 돌려봤고, `KisTrader::getDailyFills()`는
**주문이 하나도 없는 계좌에서만** 실행해봐서 레코드 파싱은 아직 미검증입니다.

이 문서는 그걸 바꾸는 절차입니다. 모의투자 계좌로, 장중에, 순서대로 진행하세요.
**설명과 다르게 동작하는 첫 단계에서 멈추십시오.**

**다음 거래일: 2026-09-21(월) 09:00~15:30 KST**

---

## 장 시작 전

```bash
cd /home/hslee/workspace/kairos
cmake --build build/Release          # 테스트도 같이 빌드됨. 여기서 실패하면 중단
./build/Release/kairos_tests
./build/Release/app/doctor
```

`doctor`가 **0 failures**여야 합니다. 텔레그램 경고 1건은 정상입니다(알림 미설정).

무엇보다 **계좌가 모의투자인지 먼저** 확인하세요:

```bash
./build/Release/app/doctor | grep -A2 "KIS credentials"
```

`mode: paper (모의투자)`로 나와야 합니다. LIVE로 나오면 중단하고 `.env`의 `KIS_MODE`를
고치세요.

---

## 1단계 — 손으로 1주 매수

가능한 가장 작은 실주문을, 전략을 거치지 않고 직접 냅니다. 실패하면 **주문 API만의
문제**라는 뜻입니다.

```bash
./build/Release/app/kis_order buy 005930 1
```

정상 출력:

```
[*] BUY 005930 x1 (market)
[+] Order accepted. No: 0000123456 at 093015
```

**실패하면** 메시지는 KIS가 보낸 것이고 원인이 적혀 있습니다. 흔한 경우:

| 메시지에 포함된 문구 | 의미 |
|---|---|
| `장시작전` / `장종료` | 09:00~15:30 밖이라 주문 접수 불가 |
| `주문가능금액` | 모의계좌 현금 부족 |
| `모의투자` | 해당 TR이 모의투자에서 지원되지 않음 |
| `초당 거래건수` | 호출 제한. 몇 초 후 재시도 |

---

## 2단계 — 증권사 쪽 확인

```bash
./build/Release/app/kis_order balance
```

005930이 수량 1로 보여야 합니다. 주문은 접수됐는데 보유가 없다면 **접수만 되고 체결이
안 된 것**입니다. 이상하다고 단정하기 전에 3단계 체결 내역을 먼저 확인하세요.

---

## 3단계 — 체결 내역 파싱 확인 ★ 가장 중요

**여기가 한 번도 실제 데이터로 돌아간 적 없는 단계입니다.** `getDailyFills()`는 KIS 명세를
보고 작성했고, "요청이 수락되고 빈 결과를 처리한다"까지만 확인했습니다.

```bash
./build/Release/app/kis_order fills
```

정상이면 주문 1건이 실제 평균가·체결수량과 함께 나오고, 이어서
`[+] Journal: 1 new fill record(s)`가 찍힙니다.

**각 칸을 예상값과 직접 대조하세요.** 필드명이 틀렸다면 **에러가 아니라 0이나 빈칸으로
나옵니다.** 특히 `avg_prvs`(평균 체결가)와 `tot_ccld_qty`(체결 수량) 두 개를 눈으로
확인하십시오.

그다음 재실행해서 중복이 안 생기는지 봅니다:

```bash
./build/Release/app/kis_order fills
```

두 번째 실행은 `0 new fill record(s)`여야 합니다.

---

## 4단계 — 저널 기록 확인

```bash
tail -3 data/trades.jsonl
```

이 주문에 대해 두 줄이 있어야 합니다. 보낼 때 기록된 `"event":"order"`와, 동기화가 쓴
`"event":"fill"`입니다. 전략에서 나온 게 아니므로 order 줄의 `"reason"`은 `"manual"`입니다.

---

## 5단계 — 되팔아서 왕복 완성

```bash
./build/Release/app/kis_order sell 005930 1
./build/Release/app/kis_order balance     # 보유가 사라져야 함
./build/Release/app/kis_order fills       # 이제 양방향 모두 보여야 함
```

왕복이 끝나면 주문 경로가 양쪽 모두 동작한다는 뜻입니다. 매도에는 거래세가 붙으므로
**가격이 그대로여도 돌아온 현금이 매수 대금보다 조금 적습니다.** 정상이며 버그가 아닙니다.

---

## 6단계 — 전략 판단만 관찰 (주문 없음)

```bash
./build/Release/app/trader --once
```

실전 프로필 5개를 실제 장중 가격에 돌려 **무엇을 하려 했는지만** 출력합니다.
`--live`가 없으므로 주문은 나가지 않습니다.

출력에서 볼 것:

- `bars=` — 몇 백 개 단위여야 함 (한두 개면 이상)
- `close=` — 실제 가격과 맞는지
- `(today, still forming)` — KIS가 오늘 봉을 내보냈다는 뜻
- `signal=` — 대부분의 날은 전부 HOLD입니다. **5년에 20회쯤 거래하는 전략이라 정상입니다**

---

## 7단계 — 전략을 통한 실주문 1건

1~6단계가 **전부 설명대로 동작한 뒤에만** 진행하세요.

```bash
./build/Release/app/trader --live --once
```

신호가 HOLD면 아무것도 나가지 않고 검증할 것도 없습니다. **어느 날이든 그럴 가능성이
높습니다.** 신호가 없는 날에 경로를 확인하고 싶다면 1단계의 수동 주문을 쓰세요 — 같은
`KisTrader::placeOrder`를 거칩니다. 7단계가 1단계보다 더 확인해주는 것은 **저널의 전략
귀속뿐**이고, 그건 dry-run에서 이미 보입니다.

---

## 다 되고 나면

그다음에야 무인 운영을 고려하세요:

```bash
./scripts/install_systemd.sh          # dry-run 상태로 설치됨
sudo loginctl enable-linger $USER     # 안 하면 로그아웃 시 중단
```

그리고 **별도로, 의식적으로** 유닛에 `--live`를 붙입니다:

```bash
systemctl --user edit --full kairos-trader.service   # ExecStart 끝에 --live 추가
systemctl --user daemon-reload
```

`kairos-trader`은 건드리지 마세요. **초단타는 보류 상태입니다** — 실제 1분봉 5일치로
측정했을 때 gross -0.97%, net -10.67%였습니다. 손실의 거의 전부가 거래비용이었고,
전략에는 그걸 감당할 엣지가 없었습니다.

---

## 문제가 생기면

```bash
./build/Release/app/doctor                       # 여기서부터
journalctl --user -u kairos-trader --since today  # systemd로 돌릴 때
tail -20 data/trades.jsonl                       # 무엇을, 왜 결정했는지
cat data/risk_state.json                         # 그날 한도가 막고 있는 건 아닌지
```

차단된 주문도 저널에 `"event":"skip"`으로 **어떤 한도가 막았는지와 함께** 남습니다.
조용히 사라지는 주문은 없습니다.
