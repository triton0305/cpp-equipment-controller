# STM32 Runtime 검증 기록

**Baseline:** `babd7c5`  
**Board:** NUCLEO-F411RE / STM32F411RET6

실물 보드 검증 결과는 [HARDWARE_VERIFICATION.md](HARDWARE_VERIFICATION.md)에 정리했습니다.  
RUN 상태 E-Stop E2E를 포함한 실제 입력·출력 검증 결과를 확인할 수 있습니다.

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

완성된 정상 Line을 기준으로 Command를 실행합니다.

---

## 3. Runtime 동작

`IoRuntime_Poll()`은 반복 호출되며 다음 작업을 처리합니다.

1. USART2 RX 처리
2. ASCII Command 처리
3. ADC 상태 확인 및 Sample 수집
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

이번 단계는 DHT11 Driver/API와 Host 검증입니다. **DHT11 HW VERIFIED가 아닙니다.**
위 1–9절의 34/34 및 Physical E2E 결과는 이전 검증 기록으로 보존합니다.
이번 전체 CTest 결과는 **36/36 PASS**입니다.

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

[Aosong DHT11 V1.3 제조사 문서](https://akizukidenshi.com/goodsaffix/DHT11_20180119.pdf)를 기준으로
습도 정수 5–95 %RH, 습도 소수 byte 0, 온도 -20–60 °C를 지원합니다.
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

**Firmware Build 대상에 포함했지만 `main.c`와 `IoRuntime_Poll()`에서 호출하지 않습니다.**
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
