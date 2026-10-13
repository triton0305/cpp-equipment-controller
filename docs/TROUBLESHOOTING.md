# Equipment Controller Troubleshooting

Linux C++ Controller와 STM32 Device I/O를 개발하며 발생한 문제를 `증상 → 원인 추적 → 조치 → 확인 결과` 순서로 정리했습니다. 통신·센서·공정·실행 기준선의 문제를 중심으로 다룹니다.

검토 기준은 2026-10-13의 [`d220977`](https://github.com/triton0305/equipment-controller/commit/d220977d43dab5e112f30b52fcacdfd49fe93a1c)입니다. `HOST VERIFIED`는 자동 테스트로 확인한 범위, `HW VERIFIED`는 실제 NUCLEO-F411RE에서 관찰한 범위입니다. Pump / Heater 출력은 LED로 확인했습니다. 테스트 수는 각 수정 당시의 기록을 사용합니다.

## 사례 목록

| 사례 | 핵심 확인 내용 | 상태 |
|---|---|---|
| 1. STM32 Command-loss Fail-safe | 상위 제어기 통신이 끊겨도 MCU에서 출력 차단 | HOST / HW VERIFIED |
| 2. DHT11 Decoder | 정상 40-bit frame이 INVALID로 거부되는 원인 특정 | HOST / HW VERIFIED |
| 3. UART Disconnect / Framing | PTY 단절 감지·동일 객체 재연결·긴 line 복구 | HOST VERIFIED |
| 4. Production Firmware 불일치 | 최신 소스와 보드에서 실행 중인 firmware 비교 | HW VERIFIED |
| 5. 오래된 실행 파일 | 실행 스크립트가 다른 build 디렉터리의 binary 사용 | 실행 경로 정정 / HW 동작 확인 |
| 6. Sensor RX에 따른 공정 급속 진행 | 센서 처리와 공정 진행 분리 | 과거 수정 HOST VERIFIED / 후속 시간 기반 공정 구현 |
| 7. UART Startup / Reconnect Alarm 6 | session 동기화 보강·후속 간헐 현상 계측 준비 | 수정 HOST VERIFIED / 후속 HW 조사 대기 |
| 8. 통신 단절 시 RED 표시 지연 | MCU local RED와 Linux ERROR 표시 인계 | 코드·회귀 테스트 반영 / 수정본 HW 확인 대기 |
| 9. STATE 추가 후 회귀 실패 | 6-command refresh와 상태 표시 명령의 계약 유지 | 회귀 PASS 복구 |

## 1. STM32 Command-loss Fail-safe

### 증상과 원인

Linux Controller가 종료되거나 UART 명령이 끊기면 STM32에 마지막 Pump / Heater ON 상태가 남을 수 있었습니다. Linux에서 `CommunicationFault`를 판단하더라도 통신이 끊긴 경로로 OFF 명령을 전달할 수 없었습니다.

출력을 적용하는 STM32에 독립적인 command freshness 감시가 필요했습니다.

### 조치

다음 6개 명령이 순서대로 250 ms 이내에 완성된 경우에만 complete refresh로 인정합니다.

```text
PUMP → HEATER → VALVE → BUZZER → RUN_LED → ERROR_LED
```

완전한 refresh 없이 1000 ms가 경과하면 STM32가 Pump / Heater / RUN 출력을 끄고 fail-safe latch를 설정합니다. 늦게 도착한 ON 명령은 차단하며, safe complete refresh로 동기화한 뒤 별도의 정상 refresh에서 ON을 허용합니다.

Timeout 검사는 RX 처리보다 먼저 수행해 deadline 경계의 오래된 ON 입력이 출력을 되살리지 않도록 했습니다. GPIO와 함께 RUN LED cache도 초기화했습니다.

### 확인 결과

Host 테스트에서 refresh 누락·순서 오류·timeout 경계·latch·stale ON 차단을 확인했습니다. 실제 보드에서도 다음 흐름을 관찰했습니다.

```text
Normal ON → command loss → 출력 OFF
→ unsafe ON 거부 → safe resync → 별도 정상 refresh에서 ON
```

**HOST / HW VERIFIED.** 1000 ms는 소프트웨어 설정값이며, 물리 출력 차단까지의 정확한 시간은 계측 대상입니다.

관련: [STM32 Runtime](../firmware/stm32-equipment-io/RUNTIME.md), [Hardware Verification](../firmware/stm32-equipment-io/HARDWARE_VERIFICATION.md), [Fail-safe 회귀 테스트](../tests/firmware/test_io_runtime.cpp)

## 2. DHT11 정상 Frame이 INVALID_DATA로 거부됨

### 증상

DHT11 독립 Hardware Harness에서 16회 중 15회가 `INVALID_DATA`였습니다.

### 원인 추적

측정 간격은 2100 ms로 유지됐고, 외부 10 kΩ pull-up 비교에서도 실패 비율이 같았습니다. 진단 Harness로 실패한 raw frame을 확인했습니다.

```text
REASON=HUMIDITY_DECIMAL
FRAME=COMPLETE
BITS=40
RAW=2D-02-19-09-51
CHECKSUM_RX=51
CHECKSUM_CALC=51
```

40-bit 수신과 checksum은 정상이었습니다. 해당 frame은 Humidity 45.2 %RH, Temperature 25.9°C를 나타냈지만 Decoder가 humidity decimal byte를 `0`만 허용하고 있었습니다.

### 조치와 확인 결과

Decimal byte 0~9를 허용하고 `raw[0] * 10 + raw[1]`로 humidity의 0.1 단위 값을 계산했습니다. 문제 frame을 회귀 test vector로 추가했습니다.

수정 당시 **CTest 37/37 PASS**, 실제 센서 **16/16 연속 OK**, `INVALID_DATA=0`을 확인했습니다. **HOST / HW VERIFIED.**

관련: [DHT11 Decoder](../firmware/stm32-equipment-io/App/Src/dht11_frame.c), [Frame 회귀 테스트](../tests/firmware/test_dht11_frame.c), [Production 경로](DHT11_PRODUCTION_PATH.md)

## 3. UART Peer Disconnect 미감지와 Framing 복구

### 증상과 원인

PTY master를 닫았는데도 `read_ok=1`, `isOpen=1`이 유지됐습니다. 기존 구현이 `read()==0`을 정상 idle처럼 처리하고 있었으며, non-blocking TTY에서는 이 반환값 하나만으로 단절을 구분하기 어려웠습니다.

### 조치

`poll()`에서 `POLLHUP / POLLERR / POLLNVAL`을 확인하면 포트를 닫고 Runtime에 실패를 전달하도록 수정했습니다. `closePort()`에서 RX buffer와 discard 상태를 초기화해 같은 객체를 다시 사용할 수 있게 했습니다.

Framing에는 256-byte line 제한을 두었습니다. 초과한 line은 다음 LF까지 폐기한 뒤 정상 line에서 재개합니다. 이후 session 동기화에는 폐기 위치도 필요해 완성 line과 `OversizedLineDiscarded` event를 byte 순서대로 전달하는 API를 추가했습니다.

### 확인 결과

수정 후 단절 시 `read_ok=0`, `isOpen=0`, 같은 객체의 재연결 후 `TEMP:31.5` 수신을 확인했습니다. Oversized 입력을 폐기한 뒤 정상 `TEMP:30.5` line으로 복구하는 테스트도 통과했습니다.

**HOST VERIFIED.** PTY 결과와 실제 USB/VCP 전체 재연결 검증은 별도로 관리합니다.

관련: [SerialTransport](../modules/transport/src/serial_transport.cpp), [Transport 테스트](../tests/transport/test_serial_transport.cpp), [Linux Recovery](COMMUNICATION_SAFETY_PHASE2.md)

## 4. 최신 DHT11 소스와 실제 Production Firmware 불일치

### 증상

DHT11 Driver 검증 후에도 Linux에서 Temperature / Humidity가 `unavailable`이었습니다. Door / E-Stop / Pressure는 정상 수신됐습니다.

### 원인 추적

Linux 표시 → Parser → raw UART → build artifact → 실제 Flash 기준선을 비교했습니다. Raw UART에는 TEMP / HUMIDITY가 없었고, 최신 ELF와 과거 BIN이 함께 존재했습니다. 보드에는 이전 Production Firmware가 실행 중이었습니다.

### 조치와 확인 결과

최신 Production ELF를 명시적으로 Flash했습니다. 이후 raw UART에서 다음 값을 수신했고 Linux STATUS에도 반영됐습니다.

```text
TEMP:21.3
HUMIDITY:46.2
```

**Production telemetry E2E — HW VERIFIED.** 독립 센서 Harness 시험 후에는 Production firmware의 재Flash까지 실행 기준선 복구에 포함했습니다.

관련: [DHT11 Production 경로](DHT11_PRODUCTION_PATH.md), [Hardware Verification](../firmware/stm32-equipment-io/HARDWARE_VERIFICATION.md)

## 5. ./run이 오래된 Build의 실행 파일을 사용함

### 증상과 원인

최신 소스에 READY 5초와 STATUS 1초 처리가 있었지만 실행 결과에 반영되지 않는 것처럼 보였습니다. 실행 스크립트는 `build-refactor/apps/controller/equipment_controller`를 사용했고, 최신 executable은 `build/apps/controller/equipment_controller`에 있었습니다.

### 조치와 확인 결과

스크립트의 실행 경로를 최신 build로 정정한 뒤 READY 지연과 약 1초 STATUS를 확인했습니다. 당시 사용자 보고값은 READY 약 5.02초였습니다.

Build 실패 후 과거 executable을 실행하는 혼선을 줄이기 위해 build와 실행을 성공 조건으로 연결했습니다. Firmware와 Host executable 모두 **소스 → 산출물 → 실제 실행 대상**을 확인하는 절차로 정리했습니다.

이 기록은 당시 실행 환경의 수정 사례입니다. 실행할 때는 `run`이 선택한 build 경로와 최신 산출물이 일치하는지 확인합니다.

관련: [READY 실물 확인 기록](../firmware/stm32-equipment-io/HARDWARE_VERIFICATION.md)

## 6. Sensor Line마다 공정이 진행되던 문제

### 증상과 원인

이전 공정 구현에서 START 후 Pump / Heater ON과 RUN 진입이 매우 빠르게 이어졌습니다. 한 UART batch의 `DOOR / ESTOP / PRESSURE / TEMP / HUMIDITY`를 처리할 때마다 Sequence까지 진행하고 있었습니다.

### 당시 조치와 확인 결과

Sensor RX에서는 값 갱신과 Safety / Interlock 검사를 수행하고, Main poll에서 공정 진행을 최대 한 번 수행하도록 분리했습니다. `LINUX_CYCLE_SAFETY_HOST`에 RX batch, START iteration, invalid / stale 측정 대기와 RUN timer 검증을 추가했고, 당시 **CTest 45/45 PASS**를 확인했습니다.

### 후속 공정 기준

[`bf68826`](https://github.com/triton0305/equipment-controller/commit/bf68826a3fcef151fffd86913d0bd17d8614393d)에서 START가 수락된 시점부터 경과시간으로 출력을 결정하는 30초 staged process로 변경했습니다.

| START 이후 경과시간 | 동작 |
|---|---|
| 0초 | PROCESS START / RUN, Pump·Heater OFF |
| 6초 | Pump ON |
| 12초 | Heater ON |
| 18초 | Heater OFF |
| 24초 | Pump OFF |
| 30초 | PROCESS COMPLETE / IDLE |

현재 테스트는 각 시간 경계, 지연된 poll의 누적 drift 방지, RX 없이 deadline 진행, 각 단계의 Fault 안전 출력을 확인하도록 구성돼 있습니다. 이 시간표는 현재 코드의 설정이며 정밀 실물 시간 계측 결과와 구분합니다.

READY / START는 fresh Door CLOSED, E-Stop OFF, 정상 통신, Temperature **20~40°C**, Pressure ADC **1000~2000** 등 자격 조건을 5초 연속 유지해야 허용합니다. 온도·압력 조건 이탈이나 invalid / stale은 qualification을 취소합니다. 이 시작 조건과 운전 중 Critical Safety 기준은 분리합니다.

관련: [공정 구현](../modules/core/src/equipment_controller.cpp), [Runtime](../apps/controller/controller_runtime.cpp), [공정 테스트](../tests/transport/test_runtime_cycle.cpp), [READY 테스트](../tests/transport/test_runtime_policy.cpp)

## 7. UART Startup / Reconnect에서 False Alarm 6

### 증상

`./run → Ctrl+C → ./run` 반복 중 첫 telemetry가 잘리거나, recovery 직후 CommunicationFault가 발생했습니다.

```text
Invalid SENSOR line: 6
Invalid SENSOR line: OOR:OPEN
EVENT sensor communication recovered
Sensor stale; communication invalid
```

### 원인 추적과 수정

UART stream 중간에서 수신을 시작하면 첫 line이 fragment일 수 있습니다. 일부 정상 field만으로 통신 복구를 선언하면 완전한 입력 경계가 확보되기 전에 운전 상태와 watchdog 판정이 시작될 수 있었습니다.

[`407969a`](https://github.com/triton0305/equipment-controller/commit/407969a00349fed7b06da05b0a45ab2981bae28b)에서 다음 session 동기화를 보강했습니다.

```text
Disconnected → Synchronizing
→ fresh DOOR → 바로 다음 valid ESTOP → Operational
```

동기화 중 Door 후보를 따로 보관합니다. 중간에 다른 field·malformed·unknown·oversized discard가 오면 후보를 폐기합니다. 정상 pair가 완성되면 함께 반영하고 recovery를 선언합니다. 정상 첫 DOOR는 후보로 사용할 수 있습니다.

Synchronizing에는 open 시점의 고정 1000 ms deadline, Operational에는 마지막 정상 sensor 수신 기준의 1000 ms watchdog을 적용합니다. 통신 복구는 기존 ERROR 해제나 RUN 자동 재개로 이어지지 않습니다.

### 확인 결과와 후속 조사

Fragment·단독 field·후보 중단·oversized gap·RUN disconnect·Error latch 보존 등의 회귀가 추가됐고 수정 당시 **CTest 45/45 PASS**였습니다.

후속 HW 관찰에서 startup / reconnect 간헐 현상이 다시 보고돼, 기존 synchronization 수정과 별도로 발생 구간을 조사하기 위한 계측을 추가했습니다. 확보된 기록만으로 이 후속 현상의 원인을 확정하지 않습니다.

[`d220977`](https://github.com/triton0305/equipment-controller/commit/d220977d43dab5e112f30b52fcacdfd49fe93a1c)의 진단은 poll / read / write / log 구간, sensor age, sync age, recovery부터 timeout까지의 시간을 기록합니다. 실행 중 고정 메모리에 보관하고 안전 종료 후 CSV로 내보냅니다.

진단 추가 당시 **Host build / CTest 46/46 PASS**와 실제 executable + PTY의 CSV 저장을 확인했습니다. **새 NUCLEO-F411RE 계측 결과는 대기 중**입니다. 후속 CSV로 출력 지연, poll 공백, RX 공백, framing 문제를 구분합니다.

관련: [Linux Recovery](COMMUNICATION_SAFETY_PHASE2.md), [UART Timing Diagnostics](UART_TIMING_DIAGNOSTICS.md), [Recovery 테스트](../tests/transport/test_runtime_recovery.cpp)

## 8. 통신 단절 시 출력은 꺼지지만 RED 표시는 늦게 켜짐

### 증상과 원인

실물 통신 단절 시험에서 Pump / Heater 출력 OFF는 확인됐지만, RED와 buzzer가 Linux 재연결 후 ERROR 명령을 받을 때 표시되는 흐름이 관찰됐습니다.

STM32 local shutdown과 Linux ERROR 표시의 책임이 분리돼 있어 통신 단절 직후 표시를 유지하는 경로가 필요했습니다.

### 조치

[`4f576d2`](https://github.com/triton0305/equipment-controller/commit/4f576d22b78049d89b9bcb70f316c5451fef27c4)에서 유효한 제어 세션이 command timeout에 도달하면 STM32가 `communication_loss_red`를 설정하도록 수정했습니다.

`STATE:SAFE`, 단독 상태 명령, `ERROR_LED:OFF`가 local RED를 지우지 못하게 했습니다. Complete refresh와 후속 Linux 운영 상태로 표시 책임을 넘기며, Linux ERROR 인계 중에도 RED를 유지합니다. Cold boot의 무수신 상태와 운전 중 통신 손실을 구분하고, local timeout 자체는 buzzer를 새로 켜지 않습니다.

### 확인 범위

회귀 테스트에는 timeout 전후, partial / malformed / unknown 입력, SAFE 재동기화, ERROR 인계, 반복 통신 손실과 RESET 후 IDLE 표시가 포함돼 있습니다. HAL fake에서 RED에 순간적인 OFF write가 발생하는지도 검사합니다.

**코드와 회귀 테스트가 저장소에 반영돼 있습니다.** 기존 command-loss 출력 차단의 HW VERIFIED와 이 RED 인계 수정본의 실물 확인을 구분해 관리합니다.

관련: [STM32 Runtime 구현](../firmware/stm32-equipment-io/App/Src/io_runtime.c), [RED 인계 테스트](../tests/firmware/test_io_runtime.cpp)

## 9. STATE Protocol 추가 후 회귀 테스트 4개 실패

### 증상과 원인

RGB 상태 표시용 `STATE` 명령을 추가하면서 당시 CTest가 **37/41 PASS**로 떨어졌습니다. TEST_27, STM32 Runtime, STM32 Fail-safe, Linux Recovery가 영향을 받았습니다.

기존 출력 계약과 fail-safe는 정확한 6-command 순서를 기준으로 complete refresh를 판단하고 있었습니다. 상태 표시 확장이 이 계약과 테스트의 기대 출력에 영향을 줬습니다.

### 조치와 확인 결과

기존 6-command refresh를 유지하고 `STATE:SAFE / IDLE / READY / RUN / ERROR`를 그 뒤의 별도 명령으로 분리했습니다. Runtime과 테스트의 기대 출력을 정리해 전체 회귀 PASS를 복구했습니다.

관련: [ASCII 직렬화](../modules/protocol/src/ascii_protocol.cpp), [STM32 Runtime 테스트](../tests/firmware/test_io_runtime.cpp)

## 검증 기록과 연결

실물 E-Stop / Door E2E, Pressure Critical, B1 RESET, DHT11 telemetry는 [Hardware Verification](../firmware/stm32-equipment-io/HARDWARE_VERIFICATION.md)에서 시험 조건과 결과를 확인할 수 있습니다. READY 상태에서 확인한 E-Stop 반응은 RUN-state E2E와 구분하고, 실제 RUN 확인 후 재시험한 결과를 기록했습니다.

이 문서의 과거 실패와 수정은 당시 사건으로 유지합니다. 현재 공정 동작은 코드·해당 commit·회귀 테스트로 연결하고, Host 결과와 실물 관찰의 범위를 각각 표시합니다.
