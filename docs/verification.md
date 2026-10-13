# Equipment Controller — Verification Report

**검증 기준일:** 2026-10-13  
**GitHub main:** `c5cbb81 feat: add independent control CLI`  
**최신 자동 테스트:** CTest **53/53 PASS**

개발 (14)·(15)·(17)의 실행 기록과 사용자 제공 실물 검증을 통합했습니다. Host 자동 테스트, 실제 보드의 정상 동작, 원인 미확정 이상 관찰과 추가 계측 항목을 구분합니다.

## 판정과 환경

| 판정 | 의미 |
|---|---|
| HOST VERIFIED | Build / CTest / Host 자동 테스트로 확인 |
| HW VERIFIED | 실제 NUCLEO-F411RE 입력·출력·실행 로그로 확인 |
| HW OBSERVED | 실제 장비에서 관찰했으나 원인·수정 효과 미확정 |
| PENDING | 추가 시험 또는 정량 계측 필요 |

Host는 Linux / WSL, MCU는 NUCLEO-F411RE, 통신은 USART2 / ST-LINK VCP입니다. Door·E-Stop·Pressure ADC·DHT11을 사용하며 Pump / Heater는 LED 출력으로 확인했습니다. CLI는 Unix Domain Socket으로 Runtime에 연결합니다. Snapshot은 Runtime이 보유한 센서값과 출력 결정을 나타냅니다.

## 1. Host Build / CTest

최신 기준선에서 전체 자동 테스트 **53/53 PASS**를 확인했습니다. 이전 단계의 Host Build와 `git diff --check` PASS 기록도 유지합니다.

검증 범위:

- Controller 상태·Interlock·Critical Safety·RESET
- READY qualification·START gate·30초 공정·Cycle Re-arm
- UART session 동기화·partial / oversized framing·timeout·Error latch
- STM32 command-loss fail-safe·local RED·DHT11·GPIO·ADC
- UART diagnostics·output coalescing
- CLI parsing·START / RESET / STATUS·STOP 로컬 차단
- Unix socket lifecycle·partial read·slow client·Runtime 종료
- 기존 stdin 명령과 UART 경로 회귀

53개는 개별 CTest entry 수입니다. Test target에는 여러 내부 시나리오가 포함됩니다. CLI process Host integration은 PTY로 STM32 입력을 모사합니다.

**판정: HOST VERIFIED**

## 2. 30초 Pump / Heater 공정

### Host 시간 경계

| START 수락 후 | 이벤트 | Pump | Heater |
|---|---|---|---|
| 0초 | PROCESS START | OFF | OFF |
| 6초 | PROCESS PUMP ON | ON | OFF |
| 12초 | PROCESS HEATER ON | ON | ON |
| 18초 | PROCESS HEATER OFF | ON | OFF |
| 24초 | PROCESS PUMP OFF | OFF | OFF |
| 30초 | PROCESS COMPLETE | OFF | OFF |

Host에서 시간 경계, 로그 순서·중복 방지, 지연된 poll, 새 START timer와 Safety regression을 검증했습니다.

### 실제 보드

실제 NUCLEO-F411RE에서 위 이벤트 순서와 공정 중 출력 변화를 확인했습니다. Runtime / CLI에서는 다음 상태가 관찰됐습니다.

| SequenceStep | Pump | Heater |
|---|---|---|
| 2 / PumpOn | ON | OFF |
| 4 / HeaterOn | ON | ON |
| 10 / HeaterOff | ON | OFF |
| 11 / PumpOff | OFF | OFF |

정상 완료 후 `State=Idle Armed=0 Rearm=1 SequenceStep=0 Pump=OFF Heater=OFF`를 확인하고 Door OPEN에 따른 다음 re-arm 단계도 관찰했습니다. 기존 기록에는 정상 완료 후 OPEN → CLOSED와 새 READY 확인이 포함됩니다.

정상 공정 완료 판정은 정상 완료가 확인된 별도 실행 로그를 사용했습니다. `Alarm=5`로 이상 종료된 실행은 이 판정에서 제외했습니다. 코드의 설정 시간과 실물 전환의 정밀 timing을 구분합니다.

**판정: HOST / HW VERIFIED — 단계 순서·출력·정상 완료**

## 3. READY Qualification

시작 조건은 Idle / Armed, fresh Door CLOSED / E-Stop OFF, Operational / 정상 통신, 유효하고 fresh한 Temperature 20~40°C와 Pressure ADC 1000~2000입니다. 모두 연속 5초 유지해야 READY가 성립합니다. 시작 자격과 Critical Fault 범위는 별도로 적용합니다.

### Pressure / Door

| 실제 Pressure | 관찰 |
|---|---|
| 약 3150 | Ready=0 · Alarm=0 |
| 1157~1241 | 정상 조건 유지 후 Ready=1 |
| 889 | Ready=0 · Alarm=0 |
| 1139~1272 | 새로운 qualification 후 Ready=1 |

압력이 정상 범위로 복귀할 때 이전 시간을 재사용하지 않고 새 5초를 요구했습니다. Pressure / Temperature가 정상이어도 Door OPEN에서는 Ready=0을 유지했습니다.

**Pressure / Door / 5초 requalification: HW VERIFIED**

### Temperature / Humidity

Temperature의 20°C / 40°C 경계와 범위 이탈은 Host regression으로 검증했습니다. 실제 보드에서는 약 22°C의 정상 범위에서 READY 진행을 관찰했습니다. 20°C 미만 / 40°C 초과의 실물 차단 시험은 PENDING입니다.

Humidity의 READY 제외와 Critical Safety 사용은 Host regression으로 검증했습니다.

**Temperature READY: HOST VERIFIED / 정상 범위 HW OBSERVED**  
**Humidity READY 제외: HOST VERIFIED**

## 4. Safety / Communication Loss

### 기존 물리 Interlock

Door Fault와 RUN 상태 E-Stop E2E에서는 실제 입력에서 UART / Controller를 거쳐 안전 GPIO 출력이 적용되는 경로를 확인했습니다. Pressure Critical과 B1 RESET의 실물 기록도 유지합니다. Temperature / Humidity Critical 전체 경계는 Host 결과와 구분합니다.

상세 근거: [Hardware Verification](../firmware/stm32-equipment-io/hardware-verification.md).

### Command-loss 출력 차단

실제 공정 중 통신을 차단했을 때 STM32의 Pump / Heater / RUN OFF를 확인했습니다. 관찰 흐름은 다음과 같습니다.

`Communication loss → STM32 local shutdown → reconnect → Linux Error indication → RESET`

통신 복구만으로 Error latch가 해제되지 않는 동작을 확인했습니다.

**판정: HW VERIFIED — 물리 출력 안전정지**

### Local RED

Host Fake-HAL에서 timeout 1000 ms의 T−1 / T / T+1, cold boot RED OFF, incomplete / SAFE traffic의 해제 방지, 기존 ERROR 보존, reconnect 인계와 local timeout의 buzzer 신규 ON 방지를 검증했습니다.

**판정: HOST VERIFIED**  
최신 firmware의 통신 단절 직후 Local RED 점등 시점에 대한 별도 최종 실물 기록은 확보되지 않았습니다. **Local RED timing: HW PENDING**

## 5. UART Startup / Reconnect

초기 실제 실행에서 `Invalid SENSOR line: 6`, `OOR:OPEN`과 recovery 후 Sensor stale / Alarm=6을 관찰했습니다.

Host synchronization 수정은 `Disconnected → Synchronizing → fresh DOOR → 인접 valid ESTOP → Operational` 계약을 사용합니다. Fragment·후보 폐기·oversized event ordering·256/257-byte 경계·split read·1000 ms sync timeout·Error latch와 READY / Re-arm 회귀를 검증했습니다.

후속 실제 실행에는 `DDOOR:OPEN`, `DODOOR:OPEN`, `PDOOR:OPEN`, `PREDOOR:OPEN` 같은 malformed line이 관찰됐습니다. 일부는 Alarm=0 / Idle을 유지했고 별도 실행에서는 recovery 후 Alarm=6이 발생했습니다.

**판정: synchronization HOST VERIFIED / 간헐 HW 이상 관찰**

## 6. UART Output Coalescing

**Commit:** [96cac56](https://github.com/triton0305/equipment-controller/commit/96cac56ecaff7c727dc7a41ddd333069a2f4c595)

일반 Sensor-driven 출력 갱신을 RX batch 단위로 모았습니다. 새 batch의 command liveness refresh, Fault 안전 출력과 실제 명령 송신 경로를 유지했습니다.

Host는 batch, 지연된 write, 수신 timestamp 유지, Safety 우선 송신, 명령 처리를 검증했습니다. 110 ms 지연을 주입한 조건에서 반복 write 누적을 줄이는 동작을 확인했습니다.

**판정: HOST VERIFIED**  
수정 후 물리 latency 감소량의 정량 HW 계측은 PENDING입니다.

## 7. 독립 CLI / IPC

**Commit:** [c5cbb81](https://github.com/triton0305/equipment-controller/commit/c5cbb81c065af3e47e83bf65d401320adfdbf856)

| 실제 보드 연결 환경의 항목 | 관찰 |
|---|---|
| `./ctl` 연결 | Connected |
| `help` | start reset status help exit |
| `status` | 실제 Runtime sensor / state snapshot 조회 |
| READY 미충족 START | REJECTED 응답 |
| READY 충족 START | OK · 정상 30초 공정 시작 |
| RESET | OK · Runtime 상태 복귀 |
| `stop` | CLI 로컬 invalid command |

CLI에서 `stop`, `STOP`, `StOp`이 IPC로 전송되지 않는 동작은 Host process 테스트로 확인했습니다. 실제 보드 환경에서도 STOP 로컬 거부를 확인했습니다. 기존 Runtime stdin STOP을 유지합니다.

**CLI 연결·HELP / STATUS·START / RESET·STOP 거부: HOST / HW VERIFIED**

Unix socket의 기본 연결과 command routing은 실물 운전 경로에서 확인했습니다. Runtime 미실행 / 종료, stale socket, partial read, slow client와 stdin / IPC regression의 상세 예외 처리는 **HOST VERIFIED**입니다. CLI EXIT 후 Runtime 유지 등의 lifecycle 실물 범위를 자동 확대하지 않습니다.

## 8. 미해결 UART 관찰

실제 장비에서 약 1초의 유효 telemetry 공백과 Alarm=6이 재현됐습니다. 계측한 Fault 직전 약 1200 ms에는 readEvents 약 112~118회, median 약 10.1 ms, maximum 약 10.3~10.6 ms, Fault sensor age 약 1000~1003 ms를 확인했습니다.

일부 Fault에는 partial / malformed line, 다른 Fault에는 Linux RX byte 공백이 있었습니다. STM32 TX / ST-LINK VCP / USB-IP 경로를 조사했지만 발생 계층은 확정하지 못했습니다.

**상태: HW REPRODUCED / Root Cause UNKNOWN / 조사 보류**

관련: [Troubleshooting](troubleshooting.md), [UART Diagnostics](uart-diagnostics.md).

## 9. Verification Matrix

| 항목 | Host | Hardware |
|---|---|---|
| Full CTest | 53/53 PASS | — |
| 30초 공정 / Pump·Heater 출력 | PASS | PASS |
| 정상 완료 / Cycle Re-arm | PASS | PASS |
| READY 5초 / Pressure / Door | PASS | PASS |
| Temperature READY | PASS | 정상 범위 관찰 |
| Humidity READY 제외 | PASS | 별도 범위 시험 없음 |
| Critical Safety | PASS | Pressure 등 기존 확인 범위 |
| UART synchronization / framing | PASS | 간헐 이상 관찰 |
| Oversized-line recovery | PASS | 직접 시험 미확인 |
| Communication-loss 출력 차단 | PASS | PASS |
| Communication-loss Local RED | PASS | timing PENDING |
| CLI 연결 / HELP / STATUS / START / RESET | PASS | PASS |
| CLI STOP 로컬 거부 | PASS | PASS |
| IPC 예외 / lifecycle | PASS | 기본 연결 외 별도 시험 미확인 |
| UART output coalescing | PASS | 감소량 정량 계측 PENDING |
| 장시간 UART 안정성 | — | PENDING |

## 10. 후속 계측

1. STM32 TX pin과 Linux RX 동시 계측으로 telemetry 공백 발생 구간 분리.
2. 장시간 UART 통신 안정성 시험.
3. 최신 STM32 Local RED의 점등 시점 확인.
4. Temperature READY 범위 이탈 실물 시험.

현재 공정·READY·Interlock·독립 CLI의 확인 범위를 유지하며, 간헐 UART 현상을 해결 완료로 기록하지 않습니다.
