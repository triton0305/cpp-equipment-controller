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
