# STM32 Runtime 검증 기록

**Baseline:** `babd7c5`  
**Board:** NUCLEO-F411RE / STM32F411RET6

실물 보드 검증 결과는 [HARDWARE_VERIFICATION.md](HARDWARE_VERIFICATION.md)에 정리했습니다.  
RUN 상태 E-Stop E2E를 포함한 실제 입력·출력 검증 결과를 확인할 수 있습니다.

**최신 변경:** Communication Safety Phase 1은 Host 검증 완료 / Hardware 미검증입니다.
기존 HW VERIFIED 기록은 이전 동작에 대한 결과이며 신규 fail-safe 검증을 의미하지 않습니다.
현재 정책과 검증 결과는 [12절](#12-communication-safety-phase-1--2026-10-12)에 기록합니다.

## 1. 검증 개요

STM32 Runtime의 GPIO, ADC, UART, ASCII Protocol과 Foreground Runtime을 구현하고 Host Test와 실제 보드에서 동작을 확인했습니다.

Host Test에서는 Fake HAL / I/O 환경에서 실제 Firmware C 코드를 실행하고, Linux 측의 실제 Parser와 Serializer를 함께 사용했습니다.

| 검증 항목 | 결과 |
|---|:---:|
| STM32 Clean Build | PASS |
| Linux Build | PASS |
| CTest | **34/34 PASS** |
| Flash / Boot / Runtime | **HW VERIFIED** |
| GPIO 출력 | **HW VERIFIED** |
| Door / E-Stop 입력 | **HW VERIFIED** |
| Pressure ADC 입력 | **HW VERIFIED** |
| USART2 양방향 통신 | **HW VERIFIED** |
| RUN 상태 E-Stop E2E | **HW VERIFIED** |
| Core Physical E2E | **HW VERIFIED** |

`main.c`에서는 CubeMX Peripheral 초기화 이후 `IoRuntime_Init()`을 호출하고, Foreground Loop에서 `IoRuntime_Poll()`을 반복 실행합니다.

Runtime 연결 코드는 USER CODE 영역에 배치했습니다.

---

## 2. Runtime 구성

STM32 Runtime은 다음 모듈로 구성됩니다.

- `device_io.c`
- `pressure_adc.c`
- `uart_io.c`
- `io_protocol.c`
- `io_runtime.c`

### `device_io.c`

STM32의 GPIO 입출력을 담당합니다.

초기화 시 Pump, Heater, Buzzer, RGB LED와 Stepper Coil을 초기 출력 상태로 설정합니다.

`DeviceIo_SetIndicators()`는 Linux Controller에서 전달되는 `RUN_LED`와 `ERROR_LED` 상태를 Green / Red 출력에 적용합니다.

주요 GPIO Mapping은 다음과 같습니다.

| 기능 | Pin |
|---|---|
| Pressure ADC | PA0 |
| USART2 TX / RX | PA2 / PA3 |
| Door | PB0 |
| E-Stop | PB1 |
| Pump | PB7 |
| Heater | PA6 |
| Buzzer | PA7 |
| ERROR Red | PB4 |
| RUN Green | PB5 |

실제 보드에서 Door / E-Stop 입력과 Pump / Heater / Buzzer / RUN / ERROR 출력을 확인했습니다.

### `pressure_adc.c`

ADC1 Single Conversion을 시작하고 이후 Poll에서 EOC 상태를 확인합니다.

Conversion은 **10 ms** 기준으로 관리하며 Tick Rollover를 고려해 시간을 계산합니다.

정상적으로 수집한 최신 ADC Sample은 Runtime의 Pressure 값으로 사용합니다.

Pressure는 **12-bit ADC Raw Count** 기준으로 처리합니다.

실물 시험에서는 PA0에 연결된 가변저항을 조작해 다음 범위를 확인했습니다.

```text
0 ~ 4095
```

최소값, 최대값과 여러 중간값이 UART를 통해 Linux까지 전달되는 것을 확인했습니다.

### `uart_io.c`

USART2의 SR / DR Register를 이용한 Foreground Polling 방식으로 RX / TX를 처리합니다.

RX Error가 발생한 Line은 정리한 뒤 다음 정상 Line부터 계속 수신합니다.

USART2 송수신은 Runtime Poll 과정에서 지속적으로 처리합니다.

### `io_protocol.c`

UART 입력을 Line 단위로 조합하고 완성된 Command를 처리합니다.

처리 기준:

- 최대 31 Character Line Buffer
- LF
- CRLF
- Fragmented Input
- Oversized Line 복구
- Binary / RX Error Line 복구

완성된 정상 Line을 파싱합니다. Phase 1부터 Pump/Heater/RUN ON은 12절의 전체 갱신 완료 조건으로 실행하며, OFF와 Buzzer/ERROR 명령은 즉시 적용합니다.

---

## 3. Runtime 동작

`IoRuntime_Poll()`은 반복 호출되며 다음 작업을 처리합니다.

1. Control timeout 및 갱신 조립 시간 검사
2. ADC 상태 확인 및 Sample 수집
3. USART2 RX 및 ASCII Command 처리
4. Sensor Report 생성
5. USART2 TX 처리

한 번의 Poll에서는 최대 다음 Byte 수를 처리합니다.

```text
RX : 32 bytes
TX : 32 bytes
```

Sensor Report는 이전 Packet 전송이 완료된 상태에서 **100 ms 간격**으로 생성합니다.

전송 중인 Packet은 이후 Poll에서도 이어서 처리하며, 동시에 RX와 ADC 처리를 계속 수행합니다.

---

## 4. ASCII Protocol

STM32와 Linux Controller는 LF 기반 ASCII Message를 사용합니다.

### STM32 → Linux

| Message | Runtime 동작 |
|---|---|
| `DOOR:CLOSED` / `DOOR:OPEN` | PB0 LOW = CLOSED |
| `ESTOP:ON` / `ESTOP:OFF` | PB1 LOW = ON |
| `PRESSURE:<integer>` | ADC Raw 0–4095 |

Sensor Packet 예시:

```text
DOOR:CLOSED
ESTOP:OFF
PRESSURE:1820
```

### Linux → STM32

| Message | Runtime 동작 |
|---|---|
| `PUMP:ON/OFF` | PB7 출력 |
| `HEATER:ON/OFF` | PA6 출력 |
| `BUZZER:ON/OFF` | PA7 출력 |
| `RUN_LED:ON/OFF` | PB5 Green 출력 |
| `ERROR_LED:ON/OFF` | PB4 Red 출력 |
| `VALVE:OPEN/CLOSED` | Command Parsing 및 수신 Count |

모든 Message는 LF로 끝납니다.

`io_runtime_rx_errors`와 `io_runtime_unsupported_valve_commands`는 Runtime 상태를 확인하기 위한 Debugger Counter로 사용합니다.

STM32 Runtime에서는 Door, E-Stop, Pressure Sensor Message를 Linux로 전달합니다.

---

## 5. Linux Controller 연동

STM32에서 전달한 Sensor Message는 Linux Parser를 거쳐 Controller 입력 상태에 반영됩니다.

Controller가 결정한 출력은 Serializer를 통해 다시 STM32로 전달되고 GPIO에 적용됩니다.

전체 경로는 다음과 같습니다.

```text
Door / E-Stop / ADC
        ↓
      STM32
        ↓
     USART2
        ↓
 Linux Parser
        ↓
 C++ Controller
        ↓
   Serializer
        ↓
     USART2
        ↓
   STM32 GPIO
```

RUN 상태 E-Stop 시험에서는 실제 Switch 입력 후 다음 출력 전환을 확인했습니다.

```text
PUMP       OFF
HEATER     OFF
RUN LED    OFF
ERROR LED  ON
BUZZER     ON
```

Linux Controller의 EmergencyStop 판단 결과가 UART를 통해 STM32에 전달되고 실제 GPIO와 LED / Buzzer 동작으로 이어지는 전체 경로를 확인했습니다.

---

## 6. Host Test

기존 30개 CTest에 STM32 Runtime 검증용 Test Suite 4개를 추가했습니다.

### `STM32_ADC_HOST`

실제 ADC 구현을 Fake HAL 환경에서 실행합니다.

검증 항목:

- ADC 최소값
- ADC 중간값
- ADC 최대값
- Conversion 진행 상태
- Timeout
- Tick Rollover
- Sample 상태 처리
- HAL Error 처리

### `STM32_PROTOCOL_CONTRACT`

STM32 Protocol과 실제 Linux Parser / Serializer 간 호환성을 확인합니다.

검증 항목:

- Linux Output **64개 조합**
- STM32 Sensor Packet → Linux Parser
- Fragmented Input
- CRLF
- Invalid Line
- Oversized Line

### `STM32_RUNTIME_HOST`

실제 Runtime과 GPIO 구현을 Fake HAL 환경에서 실행합니다.

검증 항목:

- GPIO Mapping
- 초기 출력 상태
- UART RX / TX
- RX / TX 처리량
- TX Backpressure
- RX Error Recovery
- ADC Error
- Tick Rollover
- E-Stop 입력
- Door Open 입력
- Low Pressure 입력

Fault 관련 Test에서는 실제 Linux Controller의 판단 결과를 사용합니다.

### `STM32_UART_ADAPTER_HOST`

USART2 Register 접근 코드의 동작을 검증합니다.

실제 보드 시험에서는 동일한 Runtime을 USART2 VCP에 연결해 Sensor 송신과 Output Command 수신을 확인했습니다.

---

## 7. Build / Test 재현

Repository Root에서 다음 명령으로 Build와 Test를 실행할 수 있습니다.

```sh
cmake --build firmware/stm32-equipment-io/build/Debug --clean-first
cmake --build build-refactor -j2
ctest --test-dir build-refactor --output-on-failure
```

검증 결과:

```text
STM32 Full Clean Build : PASS
Linux Build            : PASS
CTest                   : 34/34 PASS
```

STM32 Build 결과:

```text
FLASH : 19,476 B / 512 KB
RAM   : 2,144 B / 128 KB
```

기존 30개 Test와 STM32 Runtime Test Suite 4개가 모두 통과했습니다.

---

## 8. 실물 보드 검증

실제 NUCLEO-F411RE에서 다음 항목을 확인했습니다.

- Firmware Flash 및 Boot
- Door GPIO 입력
- E-Stop GPIO 입력
- Pressure ADC 입력
- Pump GPIO 출력
- Heater GPIO 출력
- Buzzer GPIO 출력
- RUN / ERROR LED 출력
- USART2 VCP 양방향 통신
- Door Fault E2E
- RUN 상태 EmergencyStop E2E

실제 Sensor 입력부터 Controller 판단과 STM32 출력까지 다음 전체 경로를 확인했습니다.

```text
Sensor Input
    ↓
STM32
    ↓
UART
    ↓
Linux Controller
    ↓
UART
    ↓
STM32 GPIO
    ↓
LED / Buzzer
```

세부 시험 결과와 GPIO Snapshot, Flash Readback, 실제 입력·출력 기록은 [HARDWARE_VERIFICATION.md](HARDWARE_VERIFICATION.md)에 정리했습니다.

---

## 9. Recorded Validation — 2026-10-08

- STM32 Full Clean Build: **PASS**
- Linux Build: **PASS**
- Full CTest: **34/34 PASS**
- FLASH: **19,476 B**
- RAM: **2,144 B**
- Generated `main.c` USER CODE 영역 연동 확인
- Door / E-Stop / ADC 실물 입력 확인
- Pump / Heater / Buzzer / RUN / ERROR 실물 출력 확인
- USART2 양방향 통신 확인
- Door Fault Physical E2E 확인
- RUN 상태 E-Stop Physical E2E 확인
- Sensor → STM32 → UART → Linux Controller → UART → STM32 GPIO 전체 제어 경로 확인

**STM32 Runtime Host Test: 34/34 PASS**  
**STM32 Runtime Physical E2E 검증 완료**

---

## 10. DHT11 Driver — 2026-10-12 / HOST VERIFIED

본 절은 DHT11 Driver/API의 최초 Host 검증 기록입니다. 최종 Driver Hardware 검증 상태는 11절에 반영했습니다.
위 1–9절의 34/34 및 Physical E2E 결과는 이전 검증 기록으로 보존합니다.
당시 전체 CTest 결과는 **36/36 PASS**입니다.

### 작업 기준과 기존 설정

- Branch: `main`, 작업 시작 HEAD: `0a590f869942fb51a1d366893b7981bedf29f49f`
- 요청 기준 `c5d6953` 이후 차이는 README, 문서, 영상/이미지 자산이며 Firmware/Linux 코드 차이는 없습니다.
- 초기 Working Tree는 깨끗했고, 수정 전 STM32 Clean Build / Linux Build / CTest 34/34가 통과했습니다.
- PC4: `DHT11_DATA`, 초기 HIGH, Output Push-Pull, No Pull, Low Speed.
- TIM5: Internal Clock, PSC=83, ARR=0xffffffff, Up Counter. 기존 코드는 초기화만 하며 Start 호출은 없습니다.
- SYSCLK/HCLK 84 MHz, APB1 42 MHz, APB1 Timer Clock 84 MHz: `84 MHz / (83+1) = 1 MHz`, Counter 1 tick = 1 µs.
- Counter 32-bit Wrap은 약 71.6분이며 unsigned 차이로 처리합니다. 실제 Counter 주기 측정은 Hardware 검증 대상입니다.
- USART2 115200 8N1, ADC 및 모든 CubeMX 초기화와 `.ioc`를 유지했습니다.
- `main.c` USER CODE Includes/2/3의 `io_runtime.h`, `IoRuntime_Init()`, `IoRuntime_Poll()` 연결을 유지했습니다.

### Driver 구성과 Public API

| 파일 | 역할 |
|---|---|
| `App/Inc/dht11.h`, `App/Src/dht11.c` | PC4/TIM5 기반 단일 Sensor Driver, Cooperative Start와 동기식 Capture |
| `App/Inc/dht11_frame.h`, `App/Src/dht11_frame.c` | HAL과 독립된 5-byte Frame Decode, 데이터 및 Result 정의 |

| API | 동작 |
|---|---|
| `Dht11_Init(&htim5)` | Sensor 전원 인가 및 `MX_TIM5_Init()` 이후 호출. 기존 TIM5를 `HAL_TIM_Base_Start()`로 시작하고 PC4를 Input으로 해제. 안정화 대기 시작 |
| `Dht11_Request()` | 초기 안정화/재시도 제한 확인 후 PC4 LOW 시작. 수락 시 `BUSY`, 이미 진행 중이어도 `BUSY` |
| `Dht11_Poll(&sample)` | LOW 대기는 즉시 `BUSY` 반환. 준비되면 Response/40-bit Capture/Decode 후 최종 Result 반환 |
| `Dht11_Cancel()` | 진행 중 Start를 Input으로 해제. 재측정 제한은 유지 |
| `Dht11_Decode(frame, length, &sample)` | 길이 5의 MSB-first 수신 Frame 검증 및 변환 |

API는 Foreground에서 단일 호출자가 사용하며 재진입/ISR 호출을 지원하지 않습니다.
TIM5와 HAL Tick이 실행 중이고 Interrupt가 활성화되어 있어야 합니다.
TIM5를 다른 코드에서 재설정/정지하거나 Counter 주파수를 변경하면 안 됩니다.
Driver는 Counter를 Reset하지 않고 Interrupt도 Mask하지 않습니다.

Driver 사용 중 PC4만 LOW Output ↔ Input으로 전환합니다. 이는 DHT11 통신 동작이며,
CubeMX Generated Initialization/Pin Assignment 변경은 없습니다.
Sensor와 HIGH 출력 충돌을 피하기 위해 Capture와 종료 시 Input으로 해제하며 HIGH를 직접 구동하지 않습니다.
외부 DATA Pull-up(모듈 내장 여부 및 MCU 입력 전압 적합성 포함)을 실제 연결 시 확인해야 합니다.

### 데이터와 Result

`Dht11Sample`:

- `uint16_t humidity_tenths_percent`: 상대습도 0.1 %RH 단위 (`530` = 53 %RH)
- `int16_t temperature_tenths_c`: 온도 0.1 °C 단위 (`237` = 23.7 °C)

최종 Decoder는 습도 5–95 %RH, 온도 -20–60 °C를 지원합니다.
실제 Sensor Frame에 맞춰 습도 소수 byte 0–9를 허용하고 `raw[0] * 10 + raw[1]`로 0.1 %RH 값을 계산합니다.
온도 byte 3의 하위 7 bit는 0–9 소수부, bit 7은 부호로 해석합니다.
기존 정수 온도 Frame도 처리합니다. 실제 장착 Sensor의 Revision/지원 범위는 Hardware 단계에서 확인합니다.
단위 표현은 Sensor의 정확도를 의미하지 않습니다.

앞 4 byte 합의 하위 8 bit를 마지막 byte와 비교한 뒤 형식/범위를 검사합니다.
출력 Sample은 `OK`일 때만 변경합니다. 실패 후 남아 있는 이전 Sample을 최신 값으로 취급하면 안 됩니다.
최종 Result는 Poll 호출자가 보관하며, 다음 idle Poll은 `NOT_READY`입니다.

| Result | 의미 |
|---|---|
| `DHT11_OK` | Init 성공 또는 유효한 Decode/Capture 결과 (Init은 Sample을 만들지 않음) |
| `DHT11_RESPONSE_TIMEOUT` | 해제 후 HIGH/Response LOW/HIGH/첫 Data LOW 전환 대기 실패 |
| `DHT11_DATA_TIMEOUT` | Data/종료 Edge 대기 실패, Capture 기한 초과, 또는 Start Poll이 30 ms 이상 지연 |
| `DHT11_CHECKSUM_ERROR` | Checksum 불일치 |
| `DHT11_INVALID_DATA` | NULL/길이/Frame 형식/범위 오류, 모호한 HIGH Pulse, 잘못된 TIM5 인자 |
| `DHT11_NOT_READY` | 미초기화, 안정화/재측정 대기, 또는 pending 요청 없는 Poll |
| `DHT11_BUSY` | Start 진행 중 |
| `DHT11_HAL_ERROR` | 실제 오류 결과가 있는 `HAL_TIM_Base_Start()` 실패 |

GPIO HAL 함수는 별도 오류 결과를 반환하지 않으므로 추가 HAL 오류 추상화를 만들지 않았습니다.

### Measurement Timing과 Blocking

- Init 이후 **2100 ms** 동안 요청을 거절합니다. Sensor를 별도로 재전원 인가했다면 다시 Init해야 합니다.
- 요청 시작 간격은 성공/실패/Cancel에 관계없이 **2100 ms 이상**입니다.
  제조사 문서의 Sampling Period >2 s에 여유를 둔 정책입니다.
- Start는 LOW 출력 후 즉시 반환합니다. HAL Tick 경과 **20 ms**부터 Capture합니다.
  1 ms Tick 양자화를 고려해도 요구 LOW 18 ms 이상을 확보합니다.
- 호출자는 Start 이후 Poll을 자주 호출하여 20–29 ms 구간에 해제해야 합니다.
  30 ms 이상 늦으면 해제 후 `DATA_TIMEOUT`이며 Capture하지 않습니다.
  Poll/Cancel 자체를 호출하지 않으면 LOW가 계속 유지되므로 호출자 Scheduling 책임이 있습니다.
- Response 구간은 Release HIGH 대기 20 µs, Response LOW 대기 100 µs,
  LOW/HIGH 종료 대기 각각 120 µs로 제한합니다.
- 40 bit를 MSB-first 수신합니다. 각 LOW/HIGH Edge 대기는 100 µs 제한입니다.
  HIGH 폭 15–40 µs는 0, 55–90 µs는 1이며 그 사이와 범위 밖 Pulse는 거절합니다.
  마지막 bit 이후 Sensor의 종료 LOW 해제도 확인합니다.
- 정상 Response/Data Capture는 대략 3–5.4 ms 규모입니다.
  개별 Timeout의 단순 합 대신 **전체 Capture 6000 µs Deadline**을 적용합니다.
- **정상 TIM5 조건의 최대 연속 Blocking 예산은 6 ms + GPIO/Decode/루프 종료 및 Interrupt 지연**입니다.
  Start 20 ms는 이 Blocking에 포함되지 않는 Cooperative 경과 시간입니다.
  제때 Poll한다면 전체 요청 경과 시간은 대략 26 ms(최초 Capture 가능 시점 기준),
  29 ms 시점에 Poll하면 약 35 ms까지 증가할 수 있습니다.
- TIM5가 멈추어도 HAL Tick이 진행한다면 **10 ms Deadline + 종료/Interrupt 지연**으로 빠져나옵니다.
  두 Clock 정지나 무제한 ISR 지연까지 보장하는 Hard Real-time WCET는 아닙니다.
  실제 최악 Blocking과 Interrupt에 의한 Pulse 판별 오차는 Hardware 측정 전입니다.

### Runtime 영향과 통합 정책

**Production Firmware에서는 DHT11을 호출하지 않습니다. 격리형 Harness는 `main.c` USER CODE에서 별도 선택합니다.**
기존 `device_io`, `io_runtime`, ADC/UART Adapter, Protocol, Linux State Machine은 변경하지 않았습니다.
따라서 현재 Runtime의 Loop/입력/출력에 추가되는 DHT11 Blocking은 없습니다.

동기식 Capture를 Poll Loop에 추가할 경우 예상 영향:

| 경로 | 영향 |
|---|---|
| Door / E-Stop | 기존 100 ms Telemetry 생성 및 입력 읽기가 Capture만큼 늦어져 Linux Safety 판단 전달도 지연 |
| UART RX | 115200 8N1은 byte당 약 86.8 µs. 현재 SR/DR Polling에 Ring Buffer/IRQ/DMA가 없어 수 ms Capture 중 ORE/명령 손실 가능 |
| UART TX | 이미 Hardware에 들어간 byte는 전송되지만 다음 byte 공급은 Capture 종료까지 지연 |
| Pressure ADC | 변환 Hardware는 계속 동작하나 EOC 회수/다음 변환 및 10 ms Timeout 판단이 늦어짐 |
| Runtime Loop | Start 대기는 Cooperative이나 Capture Poll 한 번이 최대 6 ms 예산만큼 길어짐 (Timer 이상 시 Tick fallback 10 ms) |

IRQ를 유지하는 것만으로 현재 Foreground UART 수신 손실이 해결되지는 않습니다.
따라서 주기적 Runtime Read 통합은 보류했습니다.
다음 단계에서는 승인된 범위에서 UART 수신 Buffer/IRQ/DMA 방안과 Safety Latency 예산을 먼저 결정하고,
필요하면 PC4 Edge Capture 방식 및 필요한 CubeMX/Peripheral 변경을 별도로 검토해야 합니다.
검증 전에는 실행 중인 Safety Loop에 이 동기식 Capture를 연결하지 않습니다.
이번 단계는 Linux 전달을 요구하지 않으므로 Protocol 확장이 필요하지 않으며 신규 Message도 없습니다.

### Host 검증과 Build 결과

기존 34개 Test를 유지하고 다음 2개 Suite를 추가했습니다.

- `STM32_DHT11_DECODE_HOST`: 정상 40-bit Frame, 정수/소수/음수 온도, 습도,
  경계값, Checksum Wrap/오류, 잘못된 길이/NULL/소수부/범위, 실패 시 Sample 보존.
- `STM32_DHT11_DRIVER_HOST`: 실제 Driver C와 작은 HAL Fake로 안정화/재시도 제한,
  Cooperative Start, 중복 요청, Response/Data Timeout, 모호한 HIGH Pulse,
  늦은 Poll, Cancel, HAL Start 실패, ms/µs Rollover, TIM5 정지 시 Tick Fallback을 확인.
  Fake는 고정 레벨과 Response/잘못된 첫 bit만 제공하며 정상 40-bit Waveform 전체를 모사하지 않습니다.

```text
STM32 Clean Build + final incremental build : PASS
DHT11 C compile                            : PASS
DHT11 retained-symbol link check           : PASS
Linux Build                                : PASS
Full CTest                                 : 36/36 PASS
Existing Protocol / Runtime Regression     : PASS (HOST)
```

7절 명령으로 Build와 전체 CTest를 재실행할 수 있습니다.
일반 ELF는 호출되지 않은 Driver가 `--gc-sections`로 제거되어 FLASH 19,476 B / RAM 2,144 B입니다.
별도 Link 검증에서는 Ninja의 실제 ELF Link 명령에 각 Public API
(`Dht11_Init`, `Dht11_Request`, `Dht11_Poll`, `Dht11_Cancel`, `Dht11_Decode`)의
`-Wl,--undefined=<symbol>`을 추가하고 출력 ELF/Map을 임시 디렉터리로 분리했습니다.
`arm-none-eabi-nm`으로 5개 정의를 확인했으며 FLASH 20,904 B / RAM 2,168 B였습니다.
이 Link 검증용 ELF 역시 실행 연결이 없는 Artifact이고 Flash하지 않았습니다.

### Hardware Verification 준비 및 남은 항목

현재 기본 Firmware를 Flash하는 것만으로는 DHT11 Read가 실행되지 않습니다.
별도 Hardware 단계에서 안전한 검증용 호출 경로를 준비한 후 진행해야 합니다.
장비 상태와 Door/E-Stop의 현재 상태를 먼저 확인하고 의도치 않은 기존 Fault/Buzzer 동작을 방지합니다.
검증 범위는 Firmware Flash, PC4/Pull-up/전원 연결, 실제 1 µs TIM5,
Direction Switching/Response/Pulse Width, 최초 및 반복 온습도, 간격, 센서 분리/Timeout,
UART 지속 통신, Door Telemetry, 기존 E-Stop Telemetry 경로, Runtime 정지 및 출력 변화 여부입니다.
Driver가 Sensor의 이전 측정 결과를 읽는 특성도 반복 측정 시 확인합니다.

실제 Capture 중 UART 연속 RX 안정성은 현재 동기식 구현만으로 보장할 수 없습니다.
통합 방안과 측정된 Latency를 확인한 뒤에만 운영 Runtime에 연결합니다.
Buzzer 제어/의도적 Door 또는 E-Stop Fault/Audible Test를 수행하지 않았습니다.
기존 Door/E-Stop/E-Stop→Buzzer/Core MVP Physical E2E의 HW VERIFIED 기록을 유지했고
`HARDWARE_VERIFICATION.md`는 수정하지 않았습니다.

**[READY TO COMMIT — HOST VERIFIED]**

---

## 11. DHT11 Hardware Test Harness — 2026-10-12 / HW VERIFIED

사용자가 격리형 Harness에서 연속 정상 Sample **16/16**, INVALID_DATA **0**, Checksum 전부 VALID를 확인했습니다.
온도 25.8 °C, 습도 44.3–44.5 %RH, 측정 간격 2100 ms, START_MS→DONE_MS 약 23 ms이며 비정상 정지는 없었습니다.
**DHT11 Driver: HW VERIFIED / Production Runtime Integration: NOT YET VERIFIED**
실제 결과와 Validation 수정 원인은 [HARDWARE_VERIFICATION.md](HARDWARE_VERIFICATION.md#13-dht11-driver-hardware-verification--2026-10-12)에 기록했습니다.

### 격리 구조

- `HardwareTest/dht11_hardware_test.c/.h`는 기존 DHT11 API만 사용합니다.
- `DHT11_HARDWARE_TEST` CMake Option은 기본 OFF입니다.
- 별도 `Dht11Hardware` Preset은 Option을 ON으로 하며 `build/Dht11Hardware`에 출력합니다.
- `main.c`의 Includes/2/3 USER CODE 영역만 조건부 분기합니다.
  Harness에서는 `IoRuntime_Init/Poll` 대신 Harness Init/Poll을 호출합니다.
- CubeMX 초기화 값, `.ioc`, DHT11 Driver API, Production Protocol, Linux 코드는 유지합니다. Decoder의 습도 소수부 검증을 수정했습니다.
- Buzzer/Pump/Heater/RGB/Safe-State Output API와 Fault 경로를 호출하지 않습니다.
  기존 Generated GPIO 초기 출력 설정은 유지합니다.
- Production Linux Controller를 실행하지 않습니다. USART2는 115200 8N1 Debug TX 전용이며
  Debug Line을 Production Parser에 등록하지 않습니다.

흐름은 Init → 2100 ms 안정화 → Request → Cooperative LOW → Poll/Capture → Debug TX → 다음 요청입니다.
Driver가 요청 시작 간격 2100 ms 이상을 제한합니다. Start LOW와 Capture 중에는 Debug TX를 호출하지 않습니다.
HAL UART 송신은 완료된 Sample/Error 출력에만 사용하며 100 ms Timeout이 있습니다.
송신 실패는 `dht11_hardware_uart_errors`에 누적합니다.
Init 실패 시 Sample Read 없이 대기하므로 Reset/원인 확인이 필요합니다.

Debug 출력 형식 (실측 결과가 아닌 형식 설명):

```text
DHT11:HARNESS,INIT=OK,REASON=NONE,STABILIZE_MS=2100,INTERVAL_MS=2100
DHT11:OK,SEQ=<n>,START_MS=<tick>,DONE_MS=<tick>,TEMP=<value>,HUM=<value>,CHECKSUM=VALID
DHT11:RESPONSE_TIMEOUT,SEQ=<n>,START_MS=<tick>,DONE_MS=<tick>,REASON=RESPONSE_TIMEOUT,FRAME=PARTIAL,BITS=0,RAW=<bytes>,CHECKSUM_RX=<byte>,CHECKSUM_CALC=<byte>
```

`OK` Sample은 실제 Driver의 40-bit Capture와 Decode/Checksum 검사가 모두 성공한 경우에만 출력합니다.
`SEQ`는 실패를 포함한 요청마다 증가합니다. 연속 SEQ의 OK 3개와 START_MS 차이 각각 >=2000 ms를 확인합니다.
Tick Wrap 시 unsigned 32-bit 차이를 사용합니다. DONE_MS−START_MS에는 Cooperative LOW 대기도 포함되므로
이를 Capture WCET나 정밀 µs 계측 결과로 해석하지 않습니다.

### Build 및 분리 검증

작업 시작 기준은 `main`, HEAD/origin/main `e8974ee`, Working Tree clean입니다.

Repository Root에서:

```sh
cd firmware/stm32-equipment-io
cmake --preset Dht11Hardware
cmake --build --preset Dht11Hardware
cd ../..
cmake --build firmware/stm32-equipment-io/build/Debug
cmake --build build-refactor -j2
ctest --test-dir build-refactor --output-on-failure
```

검증 결과:

- Harness Firmware Build/Link: PASS (FLASH 20,704 B / RAM 2,072 B)
- Production Debug Firmware Build/Link: PASS
- Linux Build: PASS
- CTest: 37/37 PASS (기존 Test 유지, `STM32_DHT11_DIAGNOSTIC_HOST` 추가)
- Harness ELF: DHT11/Harness 함수 존재, `IoRuntime_*`, `DeviceIo_*`, `IoProtocol_*` 심볼 없음
- Production ELF: IoRuntime 존재, Harness 없음
- Production Flash binary: 변경 전과 바이트 단위 동일
- `main.c` USER CODE 밖 Generated Code: 변경 전과 바이트 단위 동일 (기존 CRLF 유지)

### 사용자 확인 배선 및 현재 연결 상태

사용자가 확인한 구성 (계측/직접 관찰한 결과는 아님):

- VCC: 3.3 V
- GND: STM32와 공통 GND
- DATA: PC4
- 3핀 DHT11 모듈의 내장 Pull-up 사용, 외부 10 kΩ 추가하지 않음

배선 변경은 전원 제거 후 수행합니다. 기존 Door/E-Stop 상태와 출력 비활성 상태도 먼저 확인합니다.
최초 준비 시 ST-LINK/VCP가 연결되지 않았으나 이후 사용자가 실제 Hardware 측정 성공을 확인했습니다.

### WSL → ST-LINK Flash 절차 (사용자 실행)

기존 Hardware 검증 로그에서 확인한 STM32CubeProgrammer 2.23.0 경로를 사용합니다.
Windows의 `usbipd list`에서 현재 NUCLEO BUSID를 확인하고
`usbipd attach --wsl --busid <현재 BUSID>`로 연결합니다. 이전 BUSID `3-4`를 현재 값으로 가정하지 않습니다.

WSL의 Repository Root에서 먼저 연결을 확인합니다.

```sh
cd /home/kccistc/projects/cpp-equipment-controller
DHT11_PROGRAMMER=/home/kccistc/.local/share/stm32cube/bundles/programmer/2.23.0/bin/STM32_Programmer_CLI
"$DHT11_PROGRAMMER" -l stlink-only
ls -l /dev/ttyACM0
pgrep -af '(^|/)equipment_controller([[:space:]]|$)'
fuser /dev/ttyACM0
```

`pgrep`는 Controller가 없으면 출력 없이 종료코드 1을 반환합니다.
Controller나 다른 Serial Reader가 사용 중이면 해당 세션을 정상 종료한 뒤 진행합니다.
ST-LINK는 기존 NUCLEO Serial `066FFF485277504867174240`와 일치하는지 확인합니다.
다른 보드라면 아래 명령을 그대로 실행하지 않습니다.

배선/장치 확인 후 Harness binary만 Flash합니다 (실행 시 보드 Firmware가 Harness로 교체됨).

```sh
mkdir -p build/hardware-verification/dht11
set -o pipefail
"$DHT11_PROGRAMMER" -c port=SWD sn=066FFF485277504867174240 mode=UR reset=HWrst \
  -d firmware/stm32-equipment-io/build/Dht11Hardware/dht11-hardware-test.bin 0x08000000 \
  -v -rst 2>&1 | tee build/hardware-verification/dht11/flash.log
```

명령 종료코드 0과 실제 Programming/Verification 성공 메시지를 확인합니다.
실패한 Flash는 성공으로 기록하지 않습니다. 위 명령은 준비 단계에서 실행하지 않았습니다.

### UART 관찰과 판정 절차 (사용자 실행)

Controller를 실행하지 않은 상태에서 아래 명령으로 UART를 수신합니다. MCU로 명령을 보내지 않습니다.

```sh
stty -F /dev/ttyACM0 115200 raw -echo cs8 -cstopb -parenb -crtscts -ixon -ixoff
mkdir -p build/hardware-verification/dht11
timeout 20s cat /dev/ttyACM0 | tee build/hardware-verification/dht11/uart.log
```

20초 후 `timeout` 종료코드 124는 예정된 관찰 종료입니다 (pipefail 사용 시 Pipeline에도 반영).
초기 Boot Line/안정화부터 확인하려면 수신 시작 후 NUCLEO Reset Button을 한 번 누릅니다.
Reset에 따른 SEQ 재시작을 구분하고 동일 Boot의 연속 Sample을 사용합니다.

사용자가 확인할 항목:

1. Init OK와 안정화 후 최초 정상 Sample.
2. 연속 SEQ의 `DHT11:OK` 최소 3회, 매번 TEMP/HUM과 CHECKSUM=VALID.
3. 인접 START_MS 차이 각각 >=2000 ms, 계속 측정하며 비정상 정지 없음.
4. Buzzer/Pump/Heater/RGB의 의도치 않은 활성화 없음. 활성화되면 중단하고 회귀 문제로 보고.
5. 안전하게 가능한 경우에만 전원 제거 → Sensor 배선 분리 → 재전원으로 Timeout 확인.
   정상 배선 복구도 전원을 제거한 상태에서 수행. 의도적인 Door/E-Stop Fault와 Audible Test는 제외.

실제 OK Sample과 습도 Validation에 의한 INVALID_DATA 원인을 확인했고, 수정 후 연속 16회 측정에 성공했습니다.
`[DHT11 DRIVER — HW VERIFIED]` 판정은 격리형 Harness의 Driver 통신에 한정합니다.
RESPONSE_TIMEOUT / DATA_TIMEOUT / CHECKSUM_ERROR의 실제 Sensor 오류 시험은 **NOT VERIFIED**로 남깁니다.

통신 성공은 GPIO 전환/Response/Frame/Checksum/Timing의 기능적 검증입니다.
Production Runtime/Linux 동시 동작, UART RX 부하 안정성, Safety Latency, Runtime WCET,
TIM5 1 µs 정밀 오차, 온습도 절대 정확도/Calibration은 검증 범위에 포함하지 않습니다.

Harness 비활성화는 기본 Debug/Release Preset 또는 `-DDHT11_HARDWARE_TEST=OFF`로 Build합니다.
**Build만으로 보드 Firmware가 복구되지는 않습니다.** 운영 복귀는 별도 확인 후 Production ELF를 다시 Flash해야 합니다.
시험 완료 후에도 Harness가 Flash된 보드를 Production Runtime이 동작하는 보드로 취급하지 않습니다.


---

## 12. Communication Safety Phase 1 — 2026-10-12

**HOST VERIFIED / HARDWARE NOT YET VERIFIED**

시작 기준: `main`, HEAD/origin/main `e7acaee`, Working Tree clean, 기존 CTest 37개.
Linux의 State Machine/Sequence/Fault/Interlock 판단은 그대로 두고 STM32에 통신 유효성 및 물리 출력 차단만 추가했습니다.
DHT11 Driver, Production 통합, CubeMX 설정, Linux 코드, wire format은 변경하지 않았습니다.

### 구현 전 확인 및 갱신 단위

- Linux `main.cpp`는 정상 Sensor Line마다 `controller.update()` 후 전체 DeviceState를 송신합니다.
  변경된 출력만 송신하는 구조가 아니며 순서는 아래 6개로 고정되어 있습니다.
- 기존 STM32는 각 정상 Line을 즉시 GPIO에 적용했습니다. 출력 캐시는 `run_led`, `error_led`뿐이며,
  Pump/Heater/Buzzer는 별도 캐시 없이 DeviceIo를 통해 GPIO에 기록합니다.
- `DeviceIo_SetSafeState()`는 Pump/Heater/Buzzer/RGB/Stepper coil을 모두 OFF로 만듭니다.
  초기화에는 계속 사용하지만 통신 Timeout에서는 ERROR/Buzzer를 보존해야 하므로 재사용하지 않습니다.
- Valve는 인식 및 `io_runtime_unsupported_valve_commands` 증가만 수행합니다.
  초기 SafeState에서 PC0–PC3 coil을 OFF로 설정한 뒤 이를 활성화하는 Runtime 경로가 없습니다.

**Complete control refresh**는 다음 순서의 유효한 6개 Line이 중간 오류 없이 완성되는 것입니다.

```text
PUMP → HEATER → VALVE → BUZZER → RUN_LED → ERROR_LED
```

각 Line은 기존 parser의 정확한 값과 LF/CRLF 규칙을 따라야 합니다.
PUMP Line 완료 시점부터 ERROR_LED Line 완료 시점까지 경과 시간이
`IO_CONTROL_REFRESH_WINDOW_MS = 250 ms` **미만**이어야 합니다.
중복/순서 오류/누락, malformed/unknown/empty Line, binary/oversize 입력 및 UART 오류는 진행 중 조립을 무효화합니다.
새 PUMP Line은 항상 새 후보를 시작합니다. Fragmented Line은 허용하지만 단독으로 freshness를 갱신하지 않습니다.
조립 만료 또는 통신 Timeout 때 미완성 Line이 있으면 다음 LF까지 버려 경계 전후 조각의 결합을 막습니다.
조립 실패 자체가 즉시 전체 통신 Fault를 발생시키지는 않으며 마지막 유효 갱신의 Timeout은 계속 진행합니다.

**Valid control refresh / freshness 갱신 조건:**

- 동기화 완료 상태에서는 complete refresh가 완료된 Poll의 Tick으로 `last_control_refresh`를 갱신합니다.
- 초기화/Timeout latch 상태에서는 complete이면서 Pump OFF, Heater OFF, RUN_LED OFF,
  **VALVE CLOSED**인 경우에만 유효 갱신으로 인정하고 latch를 해제합니다.
- Buzzer/ERROR는 ON/OFF 모두 안전 동기화에 허용합니다. 통신 전용 Fault 표시 정책은 추가하지 않았습니다.
- 일부 명령, LED만 반복, 노이즈, Heartbeat/unknown Line 및 latch 중 unsafe complete refresh는 freshness를 갱신하지 않습니다.

### Timeout 선정과 출력 적용

상수는 `App/Inc/io_runtime.h`에서 관리합니다.

| 상수 | 초기값 | 근거 |
|---|---:|---|
| `IO_CONTROL_TIMEOUT_MS` | 1000 ms | Sensor report 100 ms, Linux loop sleep 10 ms와 scheduler/USB/UART 지연 여유; 정상 보고 약 10주기 |
| `IO_CONTROL_REFRESH_WINDOW_MS` | 250 ms | 6 Line 63–70 byte의 115200 8N1 전송 약 5.5–6.1 ms 및 host/USB 분할 전달 여유; 무기한 조각 누적 방지 |

센서 2–3 Line마다 출력 묶음이 발생할 수 있어 정상적으로 약 100 ms마다 2–3 묶음이 전달됩니다.
STM32는 고정 주기 sleep 없이 foreground에서 Poll하며 매 Poll RX/TX 각각 최대 32 byte를 처리합니다.
Production에는 DHT11 blocking 호출이 없습니다. 실제 Poll 최장 간격/USB jitter는 아직 계측하지 않았습니다.
Linux `writeAll()`의 최대 대기 예산도 1000 ms이므로 심한 송신 정체에서는 STM32 latch가 먼저/동시에 걸릴 수 있습니다.
이는 허용된 안전 동작이며 전송 함수 성공이 STM32 적용 성공을 의미하지 않습니다.
두 상수는 **HW 검증으로 조정할 초기값**이며 허용 가능한 장비 위험 시간의 인증값이 아닙니다.

`(uint32_t)(now - last_control_refresh) >= 1000`인 첫 Poll에서 RX 처리보다 먼저 차단합니다.
따라서 T−1은 출력 유지, T와 T+1은 latch 상태입니다. Tick wrap-around도 unsigned 차이로 처리합니다.
이는 HAL Tick이 진행하고 Poll이 계속 호출되는 조건이며 실제 차단 시점에는 Poll 지연이 추가됩니다.

- Pump(PB7), Heater(PA6), RUN Green(PB5): OFF.
- `run_led` 캐시도 false로 바꾸므로 이후 ERROR 명령이 과거 RUN ON을 복원하지 않습니다.
- ERROR Red와 Buzzer: Timeout 자체로 변경하지 않고 이후 Linux 명령을 계속 적용합니다.
- Valve: 구동 미지원 유지, 초기화된 coil OFF 유지.
- 수집 중 후보를 무효화하고 `io_runtime_control_valid=false`, `io_runtime_failsafe_latched=true`로 전환합니다.

초기화 직후에도 같은 invalid/latched 상태이며 `DeviceIo_SetSafeState()`의 기존 전체 OFF를 유지합니다.
Pump/Heater/RUN의 **OFF 명령은 언제나 즉시** 적용하고 **ON은 동기화된 상태의 complete refresh 완료 시에만** 적용합니다.
Buzzer/ERROR와 Valve 수신 계수는 기존 Line 단위 동작을 유지합니다.
따라서 정상 ON 적용에는 묶음 끝까지의 지연이 추가됩니다. Linux Protocol/State Machine을 변경하지 않는 의도적인 안전 정책입니다.
새 `pending` 배열은 조립용 후보이며 실제 GPIO 캐시가 아닙니다. 전체 후보의 6개 값을 모두 덮어쓴 뒤에만 읽습니다.

### Latch 해제

1. Pump/Heater/RUN OFF, Valve CLOSED인 안전한 complete refresh 수신.
2. 그 묶음의 OFF는 이미 즉시 적용되어 있음.
3. 동기화/유효 상태로 전환하고 latch 해제. 이전 ON 후보는 재적용하지 않음.
4. **이후 별도의 complete refresh**부터 ON 허용. 단독 ON은 동기화 이후에도 적용하지 않음.

Timeout 이후 Linux가 unsafe 출력 묶음만 계속 송신하면 latch는 계속 유지됩니다.
Linux는 먼저 안전 출력을 송신해야 하며 이 단계에서는 reconnect/ACK/자동 복구를 추가하지 않았습니다.
Debugger에서 `io_runtime_control_valid`, `io_runtime_failsafe_latched`를 확인할 수 있으나 wire 응답은 없습니다.

### Host 테스트 및 빌드

`STM32_COMMUNICATION_FAILSAFE_HOST`를 추가했습니다. 실제 Runtime/DeviceIo C 코드와 Linux serializer,
Fake HAL GPIO 및 제어 가능한 uint32 Tick을 사용하며 기존 `STM32_RUNTIME_HOST`의 fake를 공유합니다.
기존 TX backpressure 검증은 단독 PUMP ON 대신 전체 출력 묶음을 보내 새 ON 적용 정책을 반영합니다.

검증 범위:

- 정상/반복 complete refresh, 최초 동기화, 부분 ON 보류, 즉시 OFF.
- T−1/T/T+1, 무수신, malformed/incomplete/unknown/binary/oversized 입력 반복.
- Pump 또는 LED만 반복, 각 6개 명령의 개별 누락, 순서 오류/중복 및 UART 오류.
- Timeout 후 늦은 Pump/Heater/RUN ON 거부, unsafe complete 및 부분 safe로 latch 해제 불가.
- 각 critical ON/Valve OPEN의 독립적인 동기화 거부, ERROR/Buzzer ON인 safe refresh 허용.
- safe complete 후 latch 해제 및 과거 ON 재적용 없음, 다음 complete에서 ON 복구.
- GPIO/LED cache 일치, ERROR/Buzzer 보존, Valve coil OFF, TX backpressure 회귀.
- 250 ms 조립 시간 W−1/W/W+1, 만료 중 fragmented Line 폐기, CRLF/bytewise 수신 및 새 PUMP로 재시작.
- Deadline에 도착한 unsafe 묶음 거부, 두 시간 제한의 uint32 wrap-around.

Repository Root에서 실행:

```sh
cmake -S . -B build-refactor
cmake --build build-refactor -j2
ctest --test-dir build-refactor --output-on-failure
cd firmware/stm32-equipment-io
cmake --preset Debug -DDHT11_HARDWARE_TEST=OFF
cmake --build --preset Debug
cd ../..
git diff --check
```

결과: **38/38 CTest PASS** (기존 37개 유지 + 신규 1개), Linux/STM32 Host Build PASS,
Production ARM Debug Build/Link PASS (FLASH 20,104 B / RAM 2,160 B), `git diff --check` PASS.
기존 Protocol/Runtime/Controller/UART/DHT11 테스트 회귀 없음. Hardware Test/Flash는 수행하지 않았습니다.

### 현재 한계와 별도 Hardware Verification

Line-based protocol에는 Session ID/Sequence/ACK/Checksum/Atomic Snapshot 경계가 없습니다.
순서와 시간 제한은 최소 수신 조건일 뿐 각 Line이 동일 Linux update에서 생성되었다는 증명은 아닙니다.
전체 유효 묶음 형태로 재생되는 stale 데이터나 우연히 문법에 맞는 corruption을 구분할 수 없습니다.
안전 묶음 후 오래된 ON 전체 묶음이 도착하면 새 묶음으로 인정될 수 있습니다.
타임스탬프는 STM32가 Line을 소비하는 시각으로, Linux 생성/USB queue 진입 시각을 알 수 없습니다.
또한 GPIO는 순차 호출이며 OFF/Buzzer/ERROR는 Line마다 적용하므로 atomic output snapshot을 보장하지 않습니다.

HAL Tick 정지, MCU/foreground 정지에는 이 소프트웨어 timeout이 동작하지 않습니다.
UART는 SR/DR polling으로 IRQ/DMA ring buffer가 없어 Poll 지연에 따른 ORE 가능성이 남습니다.
이 제약 해결 및 Linux stale/reconnect/session/ACK/atomic protocol은 후속 범위입니다.

별도 Hardware 단계에서 다음을 확인합니다:

1. Production Firmware 선택, MCU 전원 유지 및 안전한 시험 출력 구성.
2. Pump/Heater/RUN ON 후 Linux↔STM32 분리/송신 중단 시 실제 GPIO OFF.
3. 마지막 complete refresh부터 차단까지 실측 시간 및 최장 Poll 지연.
4. 정상 부하/USB scheduling jitter에서 1000/250 ms의 여유와 ORE/오동작 여부.
5. LED만 반복·부분/늦은 ON·unsafe complete 재수신 중 차단 유지.
6. safe complete에서 출력 OFF로 동기화, 이후 별도 complete에서만 ON.
7. ERROR/Buzzer 유지, Valve coil OFF, 기존 Door/E-Stop/ADC/UART 회귀.

DHT11 Driver의 기존 HW VERIFIED 기록은 유지하며 이번 통신 fail-safe에는 새 Hardware 판정을 부여하지 않습니다.
