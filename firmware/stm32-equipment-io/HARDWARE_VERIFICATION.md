# Hardware Verification Report — 2026-10-08

## 1. 검증 기준

- **Board:** NUCLEO-F411RE / STM32F411RET6
- **Firmware baseline:** `0a73996`
- **Host:** Linux / WSL
- **Serial:** USART2 / ST-LINK VCP / 115200 8N1
- **Device:** `/dev/ttyACM0`

이번 검증은 기존 Firmware와 Linux Controller를 기준으로 실제 보드의 입력, UART 통신, GPIO 출력과 Controller E2E 동작을 확인했다.

Production Controller, Firmware, CubeMX, Pin, Clock 설정은 변경하지 않았으며, 실물 검증 결과를 문서에 반영했다.

기존 Host 자동 테스트 결과는 **34/34 PASS** 상태를 유지한다.

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

현재 Firmware binary를 기준으로 SWD Flash readback을 수행했다.

- Binary size: **19,476 bytes**
- Flash readback과 Firmware binary가 동일함을 확인
- 전원 재연결 후 정상 Boot 확인
- `/dev/ttyACM0`을 통해 Sensor telemetry 지속 수신 확인

Flash readback SHA-256:

```text
9f5281e14ad9ee39e1110cf0a0a11265c1cde4b58a958f63793886b50491d161
```

해당 Hash는 ELF 전체가 아닌 19,476 byte Raw Binary 기준이다.

---

## 4. GPIO 출력 검증

다음 출력을 UART 명령으로 각각 ON → OFF 전환하고 GPIO ODR / IDR을 확인했다.

| 출력 | Pin | 결과 |
|---|---|:---:|
| Pump | PB7 | PASS |
| Heater | PA6 | PASS |
| Buzzer | PA7 | PASS |
| RUN Green | PB5 | PASS |
| ERROR Red | PB4 | PASS |

총 **10회 GPIO Pin Check가 모두 일치**했다.

출력 시험 중에도 STM32 Sensor telemetry는 정상적으로 계속 수신되었다.

Buzzer는 GPIO 상태와 실제 부저음을 함께 확인했다.

---

## 5. 입력 검증

### Door

PB0에 연결된 실제 Door Switch를 조작했다.

- OPEN / CLOSED 전환 확인
- PB0 HIGH / LOW 변화 확인
- UART의 `DOOR:OPEN` / `DOOR:CLOSED` telemetry와 실제 입력 일치

### E-Stop

PB1에 연결된 E-Stop Switch를 실제로 조작했다.

```text
ESTOP:OFF
ESTOP:ON
ESTOP:OFF
ESTOP:ON
```

입력 전환과 UART telemetry가 일치하는 것을 확인했다.

### Pressure ADC

PA0에 연결된 B10K 가변저항을 직접 조작했다.

확인 범위:

```text
minimum : 0
maximum : 4095
```

최소값, 최대값과 여러 중간값을 확인했으며 주요 입력 구간에서 **175개의 서로 다른 ADC Raw 값**이 수집되었다.

현재 Pressure 값은 **12-bit ADC Raw Count**를 사용한다.

---

## 6. Linux UART Runtime 검증

기존 Linux UART 실행 파일을 실제 `/dev/ttyACM0`에 연결해 실행했다.

10초 실행 동안:

- **419개 Sensor Line 정상 수신**
- UART Read / Write 오류 없음
- Door / E-Stop / Pressure telemetry 정상 처리

해당 실행은 별도의 START 입력 없이 수행했으므로 Controller는 정상적으로 IDLE 상태를 유지했다.

---

## 7. Door Fault Physical E2E

실제 Door 입력부터 GPIO 출력까지 전체 경로를 검증했다.

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

Door Open Fault 발생 시 다음 출력이 확인되었다.

```text
PUMP       OFF
HEATER     OFF
BUZZER     ON
RUN LED    OFF
ERROR LED  ON
```

E2E Log에는 IDLE, READY, RUN 상태와 DoorOpen Fault 전환이 기록되었다.

Linux에서 결정한 출력과 비교 가능한 **296개 GPIO Snapshot 전부가 ODR / IDR과 일치**했다.

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

상태가 실제 MCU 출력에서도 확인되었다.

---

## 8. RUN 상태 E-Stop Physical E2E

이번 실물 검증의 최종 E2E 시험이다.

### Fault 발생 전

실제 입력 조건:

```text
Door     CLOSED
E-Stop   OFF
Pressure 약 1820
```

Controller가 RUN 상태에 진입했다.

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

실제 E-Stop Switch를 눌렀을 때 STM32에서 다음 메시지가 전달되었다.

```text
ESTOP:ON
```

Linux Controller:

```text
DECISION before=2 state=3 alarm=5
```

상태가 다음과 같이 전환되었다.

```text
RUN
 ↓
ERROR / EmergencyStop
```

Controller Serializer는 다음 명령을 전송했다.

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

Fault 관찰 구간의 **15개 GPIO Snapshot 모두 Controller 출력과 일치**했다.

실물에서도 다음 동작을 직접 확인했다.

- Pump LED OFF
- Heater LED OFF
- RUN Green OFF
- ERROR Red ON
- Buzzer ON

따라서 다음 전체 경로를 실물 기준으로 확인했다.

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

시험 종료 후 명시적인 All-Off 명령을 전달했고 GPIOA / GPIOB 출력이 모두 0으로 돌아온 것도 확인했다.

---

## 9. E2E Test Harness

실물 E2E 시험에는 기존에 빌드된 다음 모듈을 그대로 사용했다.

- Controller
- Protocol
- Serial Transport
- Logging

Test Harness는 실제 STM32 Sensor Message를 수신하고 기존 Controller API의 START / RESET을 호출한 뒤, Controller가 생성한 실제 Serializer 출력을 UART로 전달한다.

Controller 상태나 Fault 출력 값을 Harness에서 직접 설정하지 않는다.

RUN 상태를 충분히 관찰하기 위해 일반 Sequence 진행은 3초 간격으로 수행했으며 Door Open과 E-Stop 입력은 확인 즉시 Controller에 반영했다.

전체 E2E 실행에서는 **2,092개의 Sensor Line**을 처리했다.

Serial Port를 처음 여는 시점에 불완전한 Line 1개가 Parser에서 정상적으로 거부되었으며 이후 Sensor Message는 계속 정상적으로 처리되었다.

임시 Harness는 다음 옵션으로 정상 빌드되었다.

```text
-Wall -Wextra -Wpedantic
```

---

## 10. 트러블슈팅

### WSL USB 재연결

보드 전원을 다시 연결한 뒤 Windows에서는 ST-LINK가 Shared 상태였지만 WSL에서 USB Device가 해제된 상태를 확인했다.

다음 명령으로 ST-LINK / VCP 연결을 복구했다.

```bash
usbipd attach --wsl --busid 3-4
```

재연결 후:

- ST-LINK 접근
- `/dev/ttyACM0` 접근
- Sensor telemetry
- GPIO 출력 명령

을 다시 확인했다.

### E-Stop 배선 및 입력

초기 E-Stop 입력 확인 과정에서 PB1 상태가 변경되지 않는 현상이 있었다.

배선을 다시 점검하는 과정에서 E-Stop 연결 상태를 확인하고 수정했으며, 이후 실제 Switch 조작에 따라 다음 입력 변화가 정상적으로 수신되었다.

```text
ESTOP:OFF
ESTOP:ON
ESTOP:OFF
ESTOP:ON
```

입력 확인 후 RUN 상태에서 다시 시험해 다음 전체 경로까지 검증했다.

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

최종적으로 Pump / Heater OFF, RUN Green OFF, ERROR Red ON, Buzzer ON 동작을 실제 보드에서 확인했다.

---

## 11. 검증 자료

실물 검증 중 생성한 Raw Log, Register Capture, Flash Readback과 Test Harness 자료는 다음 위치에 보관했다.

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

Build 검증 자료이므로 Repository Stage 대상에서는 제외한다.

---

## 12. 현재 검증 범위

이번 실물 시험에서 확인한 범위:

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
- RUN-state EmergencyStop Physical E2E
- Linux Controller → STM32 GPIO 전체 제어 경로

현재 기준으로 **Core Physical E2E는 HW VERIFIED** 상태다.
