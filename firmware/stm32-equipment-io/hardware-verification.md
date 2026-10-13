# Hardware Verification

최신 검증 기준일: 2026-10-13 · GitHub `main`: `c5cbb81`.

이 문서는 실제 NUCLEO-F411RE의 입력·GPIO·센서·UART 및 공정 시험 근거를 보존합니다. 전체 Host / Hardware matrix와 독립 CLI 결과는 [Verification Report](../../docs/verification.md)에 통합합니다.

## 최신 확인 범위

| 범위 | 실물 확인 |
|---|---|
| Board / I/O | Flash · Boot · Door / E-Stop · ADC · GPIO · USART2 |
| DHT11 | 독립 frame decode · Production UART · Linux 실제값 |
| 30초 공정 | Pump / Heater 단계와 정상 완료 · Runtime / CLI 상태 |
| READY | Door 조건 · Pressure 범위 이탈·복귀 · 새 5초 qualification |
| Temperature READY | 정상 범위 약 22°C에서 진행 관찰 |
| Safety | 기존 Door / E-Stop E2E · Pressure Critical · B1 RESET |
| Communication loss | STM32 local 출력 OFF · Error 유지 / 복구 흐름 |
| Operator CLI | 연결 · HELP / STATUS · START 거부 / 수락 · RESET · STOP 로컬 거부 |

정확한 공정 전환의 시간 오차, Local RED 점등 시점, Temperature 시작 범위 밖의 시험과 장시간 UART 안정성은 추가 계측 범위입니다. 약 1초 telemetry 공백은 실물에서 재현됐으며 발생 계층은 미확정입니다.

Pump / Heater는 LED 출력으로 확인했습니다. CLI snapshot은 Controller가 보유한 값과 출력 결정입니다. 실제 관찰, Host fake 및 정량 timing을 각각 구분합니다.

## 날짜별 실물 근거

아래 기록은 각 당시 firmware / executable / 시험 조건과 결과를 유지합니다. 이후 진행한 30초 공정과 CLI 결과는 위 최신 요약과 전체 검증 보고서에 추가합니다.

<details>
<summary>2026-10-08~12 실물 로그·GPIO·Flash·DHT11 시험 기록</summary>


## 1. 검증 기준

- **Board:** NUCLEO-F411RE / STM32F411RET6
- **Firmware baseline:** `0a73996`
- **Host:** Linux / WSL
- **Serial:** USART2 / ST-LINK VCP / 115200 8N1
- **Device:** `/dev/ttyACM0`

이번 검증에서는 기존 Firmware와 Linux Controller를 기준으로 실제 보드의 입력, UART 통신, GPIO 출력과 Controller E2E 동작을 확인했습니다.

Production Controller, Firmware, CubeMX, Pin, Clock 설정은 변경하지 않았으며, 실물 검증 결과를 문서에 반영했습니다.

기존 Host 자동 테스트 결과는 **34/34 PASS** 상태를 유지합니다.

---

## 2. 검증 결과 요약

| 검증 항목 | 결과 | 확인 내용 |
|---|:---:|---|
| ST-LINK / VCP 연결 | PASS | WSL에서 NUCLEO-F411RE 및 `/dev/ttyACM0` 접근 |
| Flash | PASS | 19,476 byte Flash readback과 현재 Firmware binary 일치 |
| Boot / Runtime | PASS | 부팅 후 DOOR / ESTOP / PRESSURE telemetry 지속 수신 |
| USART2 | PASS | STM32 → Linux 센서 전송 및 Linux → STM32 출력 명령 확인 |
| Pump / Heater | PASS | UART 명령에 따른 GPIO ON / OFF 확인 |
| Buzzer | PASS | GPIO 출력 및 실제 부저음 확인 |
| RUN / ERROR LED | PASS | Green / Red GPIO ON / OFF 확인 |
| Door 입력 | PASS | 실제 OPEN / CLOSED 입력과 telemetry 일치 |
| E-Stop 입력 | PASS | 실제 스위치 OFF / ON 변화 확인 |
| Pressure ADC | PASS | 가변저항 조작으로 0–4095 및 중간 구간 확인 |
| Door Fault E2E | PASS | 실제 Door 입력 → Controller Fault → 실제 출력 전환 |
| RUN E-Stop E2E | **HW VERIFIED** | RUN → E-Stop → ERROR 및 안전 출력 동작 확인 |
| Core Physical E2E | **HW VERIFIED** | Sensor → STM32 → UART → Controller → UART → GPIO 전체 경로 확인 |

---

## 3. Flash / Boot 검증

현재 Firmware binary를 기준으로 SWD Flash readback을 수행했습니다.

- Binary size: **19,476 bytes**
- Flash readback과 Firmware binary가 동일함을 확인했습니다.
- 전원 재연결 후 정상 Boot를 확인했습니다.
- `/dev/ttyACM0`을 통해 Sensor telemetry가 지속적으로 수신되는 것을 확인했습니다.

Flash readback SHA-256:

```text
9f5281e14ad9ee39e1110cf0a0a11265c1cde4b58a958f63793886b50491d161
```

해당 Hash는 ELF 전체가 아닌 19,476 byte Raw Binary 기준입니다.

---

## 4. GPIO 출력 검증

다음 출력을 UART 명령으로 각각 ON → OFF 전환하고 GPIO ODR / IDR을 확인했습니다.

| 출력 | Pin | 결과 |
|---|---|:---:|
| Pump | PB7 | PASS |
| Heater | PA6 | PASS |
| Buzzer | PA7 | PASS |
| RUN Green | PB5 | PASS |
| ERROR Red | PB4 | PASS |

총 **10회 GPIO Pin Check가 모두 일치**했습니다.

출력 시험 중에도 STM32 Sensor telemetry는 정상적으로 계속 수신되었습니다.

Buzzer는 GPIO 상태와 실제 부저음을 함께 확인했습니다.

---

## 5. 입력 검증

### Door

PB0에 연결된 실제 Door Switch를 조작했습니다.

- OPEN / CLOSED 전환을 확인했습니다.
- PB0 HIGH / LOW 변화를 확인했습니다.
- UART의 `DOOR:OPEN` / `DOOR:CLOSED` telemetry와 실제 입력이 일치하는 것을 확인했습니다.

### E-Stop

PB1에 연결된 E-Stop Switch를 실제로 조작했습니다.

```text
ESTOP:OFF
ESTOP:ON
ESTOP:OFF
ESTOP:ON
```

입력 전환과 UART telemetry가 일치하는 것을 확인했습니다.

### Pressure ADC

PA0에 연결된 B10K 가변저항을 직접 조작했습니다.

확인 범위:

```text
minimum : 0
maximum : 4095
```

최소값, 최대값과 여러 중간값을 확인했으며 주요 입력 구간에서 **175개의 서로 다른 ADC Raw 값**이 수집되었습니다.

현재 Pressure 값은 **12-bit ADC Raw Count**를 사용합니다.

---

## 6. Linux UART Runtime 검증

기존 Linux UART 실행 파일을 실제 `/dev/ttyACM0`에 연결해 실행했습니다.

10초 실행 동안:

- **419개 Sensor Line 정상 수신**
- UART Read / Write 오류 없음
- Door / E-Stop / Pressure telemetry 정상 처리

해당 실행은 별도의 START 입력 없이 수행했으므로 Controller는 정상적으로 IDLE 상태를 유지했습니다.

---

## 7. Door Fault Physical E2E

실제 Door 입력부터 GPIO 출력까지 전체 경로를 검증했습니다.

```text
Door Switch
    ↓
STM32 GPIO Input
    ↓
UART Sensor Message
    ↓
Linux C++ Controller
    ↓
DoorOpen Fault
    ↓
Output Serializer
    ↓
UART Command
    ↓
STM32 GPIO Output
```

Door Open Fault 발생 시 다음 출력이 확인되었습니다.

```text
PUMP       OFF
HEATER     OFF
BUZZER     ON
RUN LED    OFF
ERROR LED  ON
```

E2E Log에는 IDLE, READY, RUN 상태와 DoorOpen Fault 전환이 기록되었습니다.

Linux에서 결정한 출력과 비교 가능한 **296개 GPIO Snapshot 전부가 ODR / IDR과 일치**했습니다.

DoorOpen Fault 구간의 GPIO 상태:

```text
GPIOA ODR = 0x80
GPIOB ODR = 0x10
```

즉,

- Pump OFF
- Heater OFF
- Buzzer ON
- ERROR Red ON

상태가 실제 MCU 출력에서도 확인되었습니다.

---

## 8. RUN 상태 E-Stop Physical E2E

이번 실물 검증의 최종 E2E 시험입니다.

### Fault 발생 전

실제 입력 조건:

```text
Door     CLOSED
E-Stop   OFF
Pressure 약 1820
```

Controller가 RUN 상태에 진입했습니다.

Fault 전 GPIO:

```text
GPIOA ODR = 0x40
GPIOB ODR = 0xA0
```

출력 상태:

```text
Pump       ON
Heater     ON
RUN Green  ON
Buzzer     OFF
ERROR Red  OFF
```

### E-Stop 입력

실제 E-Stop Switch를 눌렀을 때 STM32에서 다음 메시지가 전달되었습니다.

```text
ESTOP:ON
```

Linux Controller:

```text
DECISION before=2 state=3 alarm=5
```

상태가 다음과 같이 전환되었습니다.

```text
RUN
 ↓
ERROR / EmergencyStop
```

Controller Serializer는 다음 명령을 전송했습니다.

```text
PUMP:OFF
HEATER:OFF
VALVE:CLOSED
BUZZER:ON
RUN_LED:OFF
ERROR_LED:ON
```

### 실제 GPIO 결과

Fault 이후:

```text
GPIOA ODR = 0x80
GPIOB ODR = 0x10
```

확인 결과:

```text
Pump       OFF
Heater     OFF
Buzzer     ON
RUN Green  OFF
ERROR Red  ON
```

Fault 관찰 구간의 **15개 GPIO Snapshot 모두 Controller 출력과 일치**했습니다.

실물에서도 다음 동작을 직접 확인했습니다.

- Pump LED OFF
- Heater LED OFF
- RUN Green OFF
- ERROR Red ON
- Buzzer ON

따라서 다음 전체 경로를 실물 기준으로 확인했습니다.

```text
실제 E-Stop
    ↓
STM32 GPIO Input
    ↓
ESTOP:ON
    ↓
Linux Controller
    ↓
EmergencyStop
    ↓
Safe Output Command
    ↓
STM32 GPIO
    ↓
LED / Buzzer
```

**RUN-state E-Stop Physical E2E: PASS / HW VERIFIED**

시험 종료 후 명시적인 All-Off 명령을 전달했고 GPIOA / GPIOB 출력이 모두 0으로 돌아온 것도 확인했습니다.

---

## 9. E2E Test Harness

실물 E2E 시험에는 기존에 빌드된 다음 모듈을 그대로 사용했습니다.

- Controller
- Protocol
- Serial Transport
- Logging

Test Harness는 실제 STM32 Sensor Message를 수신하고 기존 Controller API의 START / RESET을 호출한 뒤, Controller가 생성한 실제 Serializer 출력을 UART로 전달합니다.

Controller 상태나 Fault 출력 값을 Harness에서 직접 설정하지 않습니다.

RUN 상태를 충분히 관찰하기 위해 일반 Sequence 진행은 3초 간격으로 수행했으며 Door Open과 E-Stop 입력은 확인 즉시 Controller에 반영했습니다.

전체 E2E 실행에서는 **2,092개의 Sensor Line**을 처리했습니다.

Serial Port를 처음 여는 시점에 불완전한 Line 1개가 Parser에서 정상적으로 거부되었으며 이후 Sensor Message는 계속 정상적으로 처리되었습니다.

임시 Harness는 다음 옵션으로 정상 빌드되었습니다.

```text
-Wall -Wextra -Wpedantic
```

---

## 10. 트러블슈팅

### WSL USB 재연결

보드 전원을 다시 연결한 뒤 Windows에서는 ST-LINK가 Shared 상태였지만 WSL에서 USB Device가 해제된 상태를 확인했습니다.

다음 명령으로 ST-LINK / VCP 연결을 복구했습니다.

```bash
usbipd attach --wsl --busid 3-4
```

재연결 후 다음 항목을 다시 확인했습니다.

- ST-LINK 접근
- `/dev/ttyACM0` 접근
- Sensor telemetry
- GPIO 출력 명령

### E-Stop 배선 및 입력

초기 E-Stop 입력 확인 과정에서 PB1 상태가 변경되지 않는 현상이 있었습니다.

배선을 다시 점검하는 과정에서 E-Stop 연결 상태를 확인하고 수정했으며, 이후 실제 Switch 조작에 따라 다음 입력 변화가 정상적으로 수신되었습니다.

```text
ESTOP:OFF
ESTOP:ON
ESTOP:OFF
ESTOP:ON
```

입력 확인 후 RUN 상태에서 다시 시험해 다음 전체 경로까지 검증했습니다.

```text
E-Stop Switch
→ PB1
→ STM32
→ UART
→ Linux Controller
→ EmergencyStop
→ UART
→ GPIO Output
```

최종적으로 Pump / Heater OFF, RUN Green OFF, ERROR Red ON, Buzzer ON 동작을 실제 보드에서 확인했습니다.

---

## 11. 검증 자료

실물 검증 중 생성한 Raw Log, Register Capture, Flash Readback과 Test Harness 자료는 다음 위치에 보관했습니다.

```text
build/hardware-verification/2026-10-08/
```

주요 파일:

```text
output-test.jsonl
input-test.jsonl
estop-test.jsonl
estop-quick.log
linux-runtime.log
e2e.jsonl
summary.json
flash-readback.log
final-state.log
run-estop-e2e.jsonl
run-estop-summary.json
run-estop-final-off.log
run-estop-too-early.jsonl
run_estop.cpp
```

Build 검증 자료이므로 Repository Stage 대상에서는 제외했습니다.

---

## 12. 현재 검증 범위

이번 실물 시험에서 다음 항목을 확인했습니다.

- STM32 Flash / Boot
- USART2 실제 양방향 통신
- Door GPIO 입력
- E-Stop GPIO 입력
- Pressure ADC Raw 입력
- Pump GPIO 출력
- Heater GPIO 출력
- Buzzer GPIO 출력
- RUN / ERROR LED 출력
- Door Fault Physical E2E
- RUN 상태 EmergencyStop Physical E2E
- Linux Controller → STM32 GPIO 전체 제어 경로

이를 통해 실제 센서 입력이 STM32와 UART를 거쳐 Linux Controller에 전달되고, Controller의 출력 명령이 다시 STM32 GPIO에 적용되는 전체 제어 경로를 실물 보드에서 확인했습니다.

**Core Physical E2E 검증 완료**


---

## 13. DHT11 Driver Hardware Verification — 2026-10-12

사용자가 실제 보드에서 확인한 최종 로그를 기준으로 격리형 Hardware Verification Harness의
DHT11 Driver 통신 결과를 기록했습니다. Production Linux Controller는 시험 경로에 포함하지 않았습니다.

### 검증 환경과 절차

- **Board:** NUCLEO-F411RE / STM32F411RET6
- **Firmware baseline:** `e8974ee` + DHT11 Harness / Diagnostic / Humidity Decoder 수정
- **DHT11:** 3핀 모듈, VCC 3.3 V, STM32 공통 GND, DATA PC4
- **Pull-up:** 모듈 내장 Pull-up, 최종 검증은 외부 10 kΩ을 제거한 원래 배선
- **Serial:** USART2 / ST-LINK VCP / 115200 8N1
- **Host / Device:** WSL / `/dev/ttyACM0`
- **Harness:** `Dht11Hardware` 빌드, Production `IoRuntime_Init/Poll` 호출 없음

Init 이후 Sensor Stabilization을 기다린 뒤 Request / Poll을 반복하고 USART2 Debug 출력을 관찰했습니다.
측정 시작 간격은 2100 ms입니다.

```text
STM32
→ PC4
→ DHT11
→ 40-bit Frame
→ Checksum Validation
→ Temperature/Humidity Decode
→ USART2 Debug Output
→ WSL
```

### 최종 실제 결과

| 검증 항목 | 결과 |
|---|---|
| 연속 정상 Sample | **16 / 16** |
| INVALID_DATA | **0** |
| Checksum | **관찰한 Sample 모두 VALID** |
| Temperature | **25.8 °C** |
| Humidity | **44.3–44.5 %RH** |
| Measurement Interval | **2100 ms** |
| DONE_MS − START_MS | **약 23 ms** |
| 비정상 정지 | **없음** |

약 23 ms는 Cooperative Start LOW 대기를 포함한 관찰 경과 시간입니다.
Capture 단독 Blocking Time, Production Runtime WCET 또는 TIM5 정밀 Timing 실측값으로 해석하지 않습니다.

### INVALID_DATA 원인과 수정

기존 Decoder는 humidity decimal byte가 0인 Frame만 허용했습니다.
실제 Sensor에서 수신한 다음 Frame은 Checksum이 정상이지만 이 조건 때문에 거절되었습니다.

```text
RAW=2D-02-19-09-51
Humidity=45.2 %RH
Temperature=25.9 °C
Checksum=VALID (0x2D + 0x02 + 0x19 + 0x09 = 0x51)
```

습도를 `humidity_tenths = raw[0] * 10 + raw[1]`로 해석하도록 수정했습니다.
습도 소수 byte는 0–9를 허용하며 기존 Checksum 및 최종 습도 범위 검증은 유지했습니다.
Pulse Threshold, Timeout, TIM5, PC4 및 CubeMX 설정은 변경하지 않았습니다.
수정 후 원래 배선에서 연속 16회 정상 Sample을 확인했습니다.

원인 분석 중 정상 연결한 외부 10 kΩ Pull-up 적용 전후 결과는 각각
`OK 1 / INVALID_DATA 15`로 동일했습니다. 외부 Pull-up 추가는 이 Validation 오류의 해결책이 아니었습니다.

### 판정 범위

PC4 실제 통신, Sensor Response, 40-bit Frame 수신, Checksum, 온도/습도 Decode와
2.1초 간격의 연속 16회 측정은 **PASS / HW VERIFIED**입니다.

Production Runtime Integration / Production Linux Controller Integration, UART RX 부하,
Door / E-Stop Safety Latency, Production Runtime WCET, 온습도 절대 정확도/Calibration,
Logic Analyzer 기반 TIM5 정밀 Timing 측정은 **NOT VERIFIED**입니다.
RESPONSE_TIMEOUT / DATA_TIMEOUT / CHECKSUM_ERROR의 실제 Sensor 오류 시험도 미확인 상태로 남깁니다.

이번 DHT11 검증에서 Buzzer Audible Test, Door Fault 또는 E-Stop Fault 실물 시험은 재수행하지 않았습니다.
기존 Door / E-Stop / E-Stop→Buzzer / Core MVP Physical E2E의 HW VERIFIED 기록은 그대로 유지합니다.

**DHT11 Driver: PASS / HW VERIFIED**

**[DHT11 DRIVER — HW VERIFIED]**

---

## 14. Communication Safety Phase 1 Hardware Verification — 2026-10-12

사용자가 실제 NUCLEO-F411RE에서 수행한 command-loss 시험 결과를 기록했습니다.
검증 문서 업데이트 기준은 HEAD/origin/main `451c19c` (`feat(stm32): add command-loss fail-safe`), Working Tree clean입니다.

### 시험 조건

- **Board:** NUCLEO-F411RE / STM32F411RET6
- **Firmware:** Production Firmware를 다시 Flash한 뒤 시험
- **Host / Device:** WSL / USB・VCP / `/dev/ttyACM0`
- **Control refresh:** 기존 Protocol의 complete refresh 사용, ON 반복 송신 간격 약 100 ms
- **관찰 출력:** Pump / Heater / RUN Green
- **설정값:** Refresh window 250 ms, communication timeout 1000 ms

MCU의 Production Runtime에서 command 송신 중단과 timeout/latch/resynchronization에 따른 물리 출력 동작을 확인했습니다.
정확한 1000 ms physical latency는 계측 장비로 측정하지 않았습니다.

### 실제 관찰 결과

| 단계 | 시험 | Pump / Heater / RUN Green 실제 결과 |
|---|---|---|
| 1 | Safe complete refresh 송신 | **OFF / OFF / OFF** |
| 2 | 정상 ON complete refresh를 약 100 ms 간격으로 반복 | **ON / ON / ON**, 약 5초 유지 확인 |
| 3 | Control command 송신 중단 | Timeout 후 **OFF / OFF / OFF** |
| 4 | Timeout latch 상태에서 unsafe ON complete refresh 송신 | **OFF / OFF / OFF** 유지 |
| 5 | Safe complete refresh 송신 | **OFF / OFF / OFF** 유지, latch 해제 |
| 6 | 이후 별도의 정상 ON complete refresh 반복 | **ON / ON / ON**, 약 5초 유지 확인 |
| 7 | 다시 command 송신 중단 | **OFF / OFF / OFF** 재확인 |

사용자가 보고한 관찰 순서:

```text
1 OFF
2 ON
3 OFF
4 OFF
5 OFF
6 ON
7 OFF
```

Safe complete refresh에서 출력이 OFF로 유지되고 이후 별도 ON complete refresh에서 재활성화된 동작으로
safe resynchronization과 latch 해제를 확인했습니다.

### 판정 및 범위

| 검증 항목 | 판정 |
|---|---|
| Command-loss physical safe shutdown | **Hardware PASS** |
| Timeout latch | **Hardware PASS** |
| Unsafe ON rejection | **Hardware PASS** |
| Safe resynchronization | **Hardware PASS** |
| Post-resync output reactivation | **Hardware PASS** |
| Repeated command-loss shutdown | **Hardware PASS** |
| Physical fail-safe behavior | **HW VERIFIED** |
| Exact timeout latency | **NOT INSTRUMENT-MEASURED** |

WSL USB/VCP detach/attach 및 `/dev/ttyACM0` disappearance/reappearance는 확인했습니다.
이는 USB/VCP 장치 상태의 확인이며 Linux automatic reconnect 검증을 의미하지 않습니다.

다음 항목은 이번 시험의 완료 범위에 포함하지 않습니다:

- Linux automatic reconnect
- Linux CommunicationFault propagation
- Sensor stale detection
- Session / Sequence / ACK
- Exact timeout latency measurement
- Full production-controller disconnect/reconnect E2E

이번 판정은 command 송신 중단과 timeout/latch/resynchronization에 따른 Pump/Heater/RUN의 물리 출력 동작에 한정합니다.
기존 Door / E-Stop / DHT11 등의 HW VERIFIED 기록은 유지합니다.

**[STM32 COMMAND-LOSS FAIL-SAFE — HW VERIFIED]**

---

## 15. Stepper Phase B Hardware Verification 준비 — 2026-10-12

**PREPARED / NOT YET VERIFIED**

Phase A Driver/Host Test 및 독립 Firmware 빌드는 완료했습니다.
이번 작업에서는 Flash, 모터 정·역회전, ULN2003 LED 및 coil OFF의 실물 관찰을 수행하지 않았습니다.
아래 절차는 사용자가 후속 Hardware Verification에서 실행할 준비 자료입니다.

### 시험 환경 및 Firmware 구분

- 기준: `3596ece` + Stepper Phase A/B 준비 변경.
- Board: NUCLEO-F411RE, Motor: 28BYJ-48, Driver: ULN2003.
- 인수인계 배선: IN1=PC0, IN2=PC1, IN3=PC2, IN4=PC3. DHT11 PC4 유지.
- 모터 전원 정격·실제 배선·공통 GND는 시험 전에 사용자가 확인. 이번 작업에서 직접 확인하지 않음.
- 무부하 정·역회전 시험으로 시작하며 OPEN/CLOSED 위치, 회전 각도 및 실제 CW/CCW를 미리 확정하지 않음.
- USART2 / ST-LINK VCP / 115200 8N1 / WSL `/dev/ttyACM0`.
- 8-phase half-step, 초기 2 ms/half-step, 명령당 512 phase 출력, 마지막 위상 유지 후 coil OFF.

`StepperHardware`는 Production Runtime을 실행하지 않는 **독립 시험 Firmware**입니다.
Door/E-Stop 입력 보고와 Production Communication Fail-safe는 이 Firmware에 없습니다.
시험 중 UART가 끊겨도 진행 중인 유한 이동은 완료 후 coil OFF합니다.
Production Linux Controller와 다른 Serial Writer를 종료하고 시험합니다.
모터 전원/배선 문제나 의도하지 않은 구동을 발견하면 시험을 중단하고 원인을 보고합니다.

### Build 및 Flash 준비

Repository Root에서:

```sh
cd firmware/stm32-equipment-io
cmake --preset StepperHardware
cmake --build --preset StepperHardware
```

Artifact:

```text
firmware/stm32-equipment-io/build/StepperHardware/stm32-equipment-io.elf
firmware/stm32-equipment-io/build/StepperHardware/stepper-hardware-test.bin
```

사용자가 Hardware 시험을 진행하기로 결정한 뒤, Repository Root에서 대상 ST-LINK를 확인합니다.
기존 설치 경로를 사용하며 현재 장치의 Serial을 확인한 값으로 대입합니다.

```sh
STEPPER_PROGRAMMER=/home/kccistc/.local/share/stm32cube/bundles/programmer/2.23.0/bin/STM32_Programmer_CLI
"$STEPPER_PROGRAMMER" -l stlink-only
# 아래 값은 확인한 보드 Serial로 교체한 뒤 실행
STEPPER_STLINK_SERIAL='<확인한_SERIAL>'
"$STEPPER_PROGRAMMER" -c port=SWD sn="$STEPPER_STLINK_SERIAL" mode=UR reset=HWrst \
  -d firmware/stm32-equipment-io/build/StepperHardware/stepper-hardware-test.bin 0x08000000 \
  -v -rst
```

Programming/Verification 성공과 Boot 직후 coil OFF를 직접 확인합니다.
이 문서 작성 과정에서는 위 Flash 명령을 실행하지 않았습니다.

### UART 시험 시작과 관찰

터미널 A에서 Serial 수신을 시작하고 보드를 Reset하여 READY를 확인합니다:

```sh
stty -F /dev/ttyACM0 115200 raw -echo cs8 -cstopb -parenb -crtscts -ixon -ixoff
cat /dev/ttyACM0
```

Boot 보고 형식:

```text
STEPPER BENCH READY COILS_OFF POSITION_UNKNOWN
```

각 단계는 사용자가 명시적으로 실행하고 실물 결과를 확인한 뒤 다음으로 넘어갑니다.
터미널 B에서 정방향 요청:

```sh
printf 'FWD\n' > /dev/ttyACM0
```

`START FWD 512` → `DONE COILS_OFF`와 함께 실제 축 회전·LED 패턴 및 완료 후 LED OFF를 관찰합니다.
소프트웨어 기준 예상 시간은 약 1026 ms + Poll 지연입니다. 실제 latency 계측값이 아닙니다.
완료를 확인한 뒤 역방향 요청:

```sh
printf 'REV\n' > /dev/ttyACM0
```

실제 축이 반대 방향으로 회전하는지와 완료 후 coil OFF를 확인합니다.
이동 중 `FWD`/`REV`는 `BUSY`로 거부되며 재시작/대기 이동을 만들지 않습니다.
완료 후 새 FWD/REV Line은 별도 시험 요청입니다. 상태 갱신처럼 지속 반복 송신하지 않습니다.

중간 정지는 다음 명령으로 FWD 요청 약 250 ms 후 STOP을 송신합니다:

```sh
python3 - <<'PY'
import os
import time
fd = os.open('/dev/ttyACM0', os.O_WRONLY | os.O_NOCTTY)
try:
    os.write(fd, b'FWD\n')
    time.sleep(0.25)
    os.write(fd, b'STOP\n')
finally:
    os.close(fd)
PY
```

실제로 이동 중 정지가 되었는지 확인합니다. Host scheduling 지연으로 이미 완료된 경우에는
중간 정지 PASS로 기록하지 않고 다시 시험합니다. `STOPPED COILS_OFF` 이후 최소 2초 동안
coil OFF와 무재시작을 관찰한 뒤 `REV` 또는 `FWD`를 명시적으로 송신해 재구동을 확인합니다.
마지막에는 `STOP\n`을 송신하고 코일 OFF를 확인합니다.

### 실제 결과 기록표 — 현재 전 항목 미실행

| 단계 | 관찰/판정 기준 | 실제 결과 |
|---|---|---|
| 1 | Boot 후 명령 전 ULN2003 coil LED OFF, 축 무구동 | NOT YET VERIFIED |
| 2 | 명시적 FWD 수신 후에만 구동 시작 | NOT YET VERIFIED |
| 3 | FWD 512 phase 동안 실제 회전, 관찰 CW/CCW 기록 | NOT YET VERIFIED |
| 4 | FWD 완료 후 coil OFF | NOT YET VERIFIED |
| 5 | REV 512 phase 동안 실제 반대 회전 | NOT YET VERIFIED |
| 6 | REV 완료 후 coil OFF | NOT YET VERIFIED |
| 7 | 이동 중 StopAndRelease에 따른 회전 중단 | NOT YET VERIFIED |
| 8 | 정지 후 coil OFF 유지 및 자동 재시작 없음 | NOT YET VERIFIED |
| 9 | 새 요청으로 정상 재구동 및 완료 후 OFF | NOT YET VERIFIED |
| 10 | 실제 축/ULN2003 LED 관찰 기록 | NOT YET VERIFIED |

UART `DONE`이나 Host Test PASS만으로 위 항목을 PASS로 변경하지 않습니다.
512개 전기적 위상 출력과 실제 축의 정확한 이동량은 구분합니다.
실제 방향, 스텝 간격/각도 계측, 위치 정확도, 부하 토크, 탈조 여부, OPEN/CLOSED 도달은 현재 미확인입니다.
이 시험은 Production VALVE/센서/E-Stop/command-loss와 Stepper의 통합 검증이 아닙니다.
기존 Door/E-Stop/DHT11 및 STM32 command-loss의 HW VERIFIED 기록은 그대로 유지합니다.

### Production 복귀

시험 완료 후 Production을 다시 Build합니다:

```sh
cd firmware/stm32-equipment-io
cmake --preset Debug -DSTEPPER_HARDWARE_TEST=OFF -DDHT11_HARDWARE_TEST=OFF
cmake --build --preset Debug
```

Build만으로 보드 Firmware가 바뀌지 않습니다. 대상 보드를 확인한 뒤 사용자 결정으로
`build/Debug/stm32-equipment-io.elf`를 Flash하고 Production 동작을 확인해야 복귀가 완료됩니다.


## 16. Controller Runtime 실물 결과 — 2026-10-12 기록

사용자가 이번 변경본의 HW Verification 결과로 전달한 관찰을 기록한다.
에이전트가 직접 측정하거나 flash한 결과는 아니다. 기존 검증 기록은 유지한다.

| 항목 | 입력 조건 | 실제 동작 | 측정 시간 | 사용자 판정 / 기록 |
|---|---|---|---|---|
| IDLE indication | 프로그램 실행, UART 연결 | RGB Blue | 측정 없음 | **PASS / HW VERIFIED** |
| READY delay | Door CLOSED, E-Stop OFF | 5초 후 RGB Yellow | **5.02 s** | **PASS / HW VERIFIED** |
| RUN duration | READY 상태에서 START, 정상 sequence 완료 | RUN Green 유지 후 자동 Shutdown | **30.1 s** | **PASS / HW VERIFIED** |

측정 방법·오차·정확한 시작/종료 기준, 시험 시각, 실행 binary 식별 정보는 미제공이다.
30.1 s는 보고된 실측값으로 유지하며 정확히 30.000 s를 입증한 것으로 기록하지 않는다.
이번 보고는 대기/Shutdown 시 Pump·Heater 출력, Shutdown 후 IDLE Blue,
조기 START 거부, Door/E-Stop fault, buzzer, B1 RESET, command-loss 재검증을 포함하지 않는다.
해당 항목은 이번 변경본에서 미검증 상태를 유지한다.

Production DHT11 온도/습도 경로 미연결 제한도 유지한다.
사용자의 “정상 sequence 완료” 보고를 실제 DHT11 온도 확인 PASS로 확대하지 않는다.
Stepper/OLED 상태는 변경하지 않았다.

전체 체크리스트 및 남은 항목:
[Controller runtime 정책 검증](../../docs/controller-runtime.md).

## 17. DHT11 운영 경로 연결 — Host 검증, HW 대기

2026-10-12 후속 구현으로 production DHT11 temperature/humidity → UART → Linux
SensorState → 1초 snapshot 경로를 연결했다. 앞선 절의 운영 경로 미연결 내용은
그 관찰을 기록한 시점의 상태이며, 현재 구현/Host 검증 상태는 아래 보고서를 따른다.

- 전체 CTest: **44/44 PASS** (기존 42개 포함).
- STM32 production Debug 및 독립 DHT11/Stepper firmware build: PASS.
- 실제 운영 DHT11 경로 및 UART capture hook: **HW 미검증**.
- 기존 DHT11 독립 harness와 사용자 제공 IDLE/READY/RUN 실물 기록은 유지한다.
  이 기록을 이번 운영 경로의 HW VERIFIED로 확대하지 않는다.

[구현 경로, freshness/실패 정책, 테스트 및 실물 확인 항목](../../docs/dht11-integration.md).

## 18. DHT11 운영 경로 사용자 관찰 및 후속 Cycle Safety — 2026-10-12 기록

사용자가 후속 요청에 제공한 실제 HW 관찰:

| 관찰 항목 | 사용자 제공 결과 | 기록 |
|---|---|---|
| Production TEMP/HUMIDITY UART 수신 | 실제 HW에서 수신 확인 | **PASS / HW VERIFIED (사용자 관찰)** |
| Linux 1초 STATUS의 Temperature/Humidity | 실제값 표시 확인 | **PASS / HW VERIFIED (사용자 관찰)** |

에이전트 직접 측정 결과가 아니다. 관찰 당시 binary 식별, 실제 수치, 측정 시각/오차는 미제공이다.
이 두 항목을 DHT 오류/만료/복구, UART timing, 새 critical/sequence/re-arm 정책의 HW 검증으로
확대하지 않는다. 앞선 DHT 운영 경로 HW 대기 기록은 당시 상태이며, 이 표가 제공된 관찰만 갱신한다.

이후 구현한 [Cycle Safety 정책](../../docs/process-control.md)은 Host **45/45 PASS**,
STM32 Debug 및 기존 HW target build 확인 완료이고 **새 정책의 실물 검증은 대기**이다.
기존 IDLE/READY/RUN 및 command-loss의 역사적 검증 기록은 유지한다.


</details>
