# Equipment Controller Troubleshooting

Equipment Controller 개발 과정에서 실제로 재현한 문제 중 **증상, 원인 추적, 수정 또는 검증 근거가 남아 있는 사례**를 정리했습니다.

단순 명령 오타나 개발환경 조작 실수보다 Controller, UART, Sensor, Safety, Hardware 계층의 문제를 중심으로 기록합니다.

---

## 1. STM32 Command-loss Fail-safe

### 증상

Linux Controller가 종료되거나 UART 명령이 끊겼을 때 STM32에 마지막으로 적용된 Pump / Heater ON 상태가 남을 가능성이 있었습니다.

Linux 측에서 `CommunicationFault`를 발생시키더라도 Linux 자체가 명령을 전송할 수 없는 상태에서는 실제 GPIO 출력을 안전하게 내린다는 보장이 부족했습니다.

### 원인

장비 출력의 최종 적용 주체는 STM32이므로, 통신 손실에 대한 안전 정지는 Linux Controller와 독립적으로 STM32 내부에서도 수행해야 했습니다.

### 조치

STM32 Runtime에 다음 구조를 적용했습니다.

```text
6-command complete refresh
→ command freshness 관리
→ 1000 ms command timeout
→ STM32 local safe-state
→ fail-safe latch
→ stale ON command 차단
→ safe complete refresh
→ 정상 제어 재활성화
```

Pump / Heater / RUN ON은 complete refresh가 정상적으로 수신된 경우에만 적용하고, timeout 이후에는 안전한 전체 출력 상태가 다시 동기화되기 전까지 ON 출력을 재적용하지 않도록 구성했습니다.

### 검증

Host regression test와 실제 보드에서 다음 흐름을 확인했습니다.

```text
Normal ON
→ command loss
→ Pump / Heater / RUN OFF
→ unsafe ON reject
→ safe resynchronization
→ 정상 재활성화
```

**결과: HOST / HW VERIFIED**

---

## 2. Linux UART Disconnect / Reconnect

### 증상

PTY peer를 종료한 뒤에도 Linux `SerialTransport`가 포트를 열린 상태로 유지하는 경우가 있었습니다.

초기 관찰:

```text
peer disconnect
read_ok=1
isOpen=1
```

이 상태에서는 실제 연결이 끊겼는데도 Controller가 transport를 정상 연결로 해석할 수 있었습니다.

### 원인 추적

`read() == 0`만으로는 non-blocking serial 환경에서 실제 disconnect와 일시적인 무데이터 상태를 안전하게 구분하기 어려웠습니다.

따라서 단순히 `read()==0`을 disconnect로 처리하지 않고 `poll()`의 상태를 함께 확인했습니다.

```text
POLLHUP
POLLERR
POLLNVAL
```

### 조치

해당 poll event가 발생하면 transport를 닫고 상위 Runtime이 reconnect 절차로 진입하도록 수정했습니다.

### 검증

수정 후 disconnect 시:

```text
read_ok=0
isOpen=0
```

을 확인했고, 동일한 Transport 객체를 다시 열어:

```text
TEMP:31.5
```

와 같은 정상 Sensor line을 다시 수신했습니다.

**결과: HOST VERIFIED**

---

## 3. DHT11 `INVALID_DATA` Decoder Bug

### 증상

DHT11 독립 Hardware Harness에서 통신 자체는 이루어졌지만 대부분의 측정값이 `INVALID_DATA`로 처리됐습니다.

```text
OK 1
INVALID_DATA 15
```

### 원인 추적

처음부터 배선 불량으로 결론 내리지 않고 Driver scheduling, pull-up, frame 수신, checksum 순서로 확인했습니다.

```text
2100 ms scheduling 정상
↓
10 kΩ pull-up 변경 전/후 결과 동일
↓
FRAME=COMPLETE
BITS=40
↓
Checksum 일치
↓
RAW=2D-02-19-09-51
```

수신된 frame 자체는 정상이었습니다.

계속 추적한 결과:

```text
Humidity integer = 45
Humidity decimal = 2
```

였지만 Decoder가 humidity decimal byte가 `0`인 경우만 유효한 값으로 처리하고 있었습니다.

### 조치

DHT11의 decimal humidity 값을 정상 데이터로 처리하도록 Decoder 조건을 수정했습니다.

### 검증

수정 후:

```text
CTest 37/37 PASS
16/16 consecutive HW samples OK
INVALID_DATA 0
```

을 확인했습니다.

**결과: HOST / HW VERIFIED**

---

## 4. DHT11 Production Firmware Mismatch

### 증상

DHT11 Driver 자체의 Hardware 검증 이후에도 실제 Linux Runtime에서는 다음과 같이 표시됐습니다.

```text
Temperature=unavailable
Humidity=unavailable
```

동시에 Pressure / Door / E-Stop telemetry는 정상적으로 수신됐습니다.

### 원인 추적

Driver를 다시 수정하기 전에 데이터 경로를 계층별로 확인했습니다.

```text
Linux Parser
↓
Raw UART
↓
WSL USB/IP
↓
STM32 UART telemetry
↓
Build artifact
↓
실제 Flash된 firmware
```

Raw UART에서 TEMP / HUMIDITY가 출력되지 않는 것을 확인했고, 최신 build artifact는 존재했지만 실제 보드에는 이전 Production Firmware가 동작하고 있음을 확인했습니다.

### 조치

최신 Production Firmware를 다시 Flash했습니다.

### 검증

Flash 이후 실제 UART에서:

```text
TEMP:21.3
HUMIDITY:46.2
```

를 확인했습니다.

Linux Runtime의 1초 STATUS에도 Temperature / Humidity가 정상적으로 반영됐습니다.

이 사례를 통해 소스 코드와 build 결과뿐 아니라 **실제로 실행 중인 firmware 기준선**을 함께 확인해야 한다는 점을 검증했습니다.

**결과: HW VERIFIED**

---

## 5. Sensor Line마다 Sequence가 진행되던 문제

### 증상

START 직후 공정이 단계적으로 진행되지 않고 매우 짧은 시간 안에 RUN까지 도달했습니다.

실제 관찰:

```text
START
→ CheckDoor
→ Pump ON
→ Heater ON
→ RUN
```

로그에서는 여러 SequenceStep이 거의 연속으로 통과하는 형태가 나타났습니다.

### 원인

STM32가 하나의 reporting cycle에서 여러 Sensor line을 보냅니다.

```text
DOOR
ESTOP
PRESSURE
TEMP
HUMIDITY
```

Linux Runtime에서 각 Sensor line을 처리할 때마다 Sequence progression까지 수행하면서, 하나의 UART batch가 여러 공정 단계를 진행시키고 있었습니다.

### 조치

Sensor ingestion과 Sequence transition의 책임을 분리했습니다.

```text
Sensor RX
→ 값 갱신
→ Safety / Interlock 즉시 평가

Main Poll
→ 최대 1회 Sequence Transition
```

Sequence는 한 번의 poll에서 최대 한 단계만 진행하도록 변경했습니다.

### 검증

Host regression에서 다음을 확인했습니다.

- START 직후 `CheckDoor` 유지
- 한 poll당 최대 한 단계 진행
- Pressure 조건 대기
- Temperature invalid / stale 상태에서 진행 차단
- 유효한 Temperature 확인 후 `ProcessStart`
- 실제 RUN 진입 이후 30초 timer 시작

**결과: HOST VERIFIED**

---

## 6. E-Stop / Door Physical E2E Verification

### 문제

Safety 기능은 단순 Host Test만으로 완료 처리하지 않고 실제 물리 입력부터 GPIO 출력까지 전체 경로를 확인할 필요가 있었습니다.

초기 E-Stop 시험에서는 입력 조건이나 시험 시점이 명확하지 않은 경우가 있었기 때문에 원인을 바로 배선 문제로 확정하지 않았습니다.

또한 READY 단계에서 E-Stop 반응이 확인됐더라도 이를 RUN-state E-Stop E2E 완료로 처리하지 않았습니다.

### 검증 기준

실제 전체 경로를 다음과 같이 확인했습니다.

```text
Physical input
→ STM32 GPIO
→ UART telemetry
→ Linux Controller
→ Alarm / ERROR
→ UART output command
→ STM32 GPIO output
→ LED / Buzzer observation
```

### E-Stop 결과

```text
RUN
→ physical E-Stop
→ EmergencyStop ERROR
→ Pump OFF
→ Heater OFF
→ RUN Green OFF
→ ERROR Red ON
→ Buzzer ON
```

### Door 결과

RUN 중 실제 Door를 OPEN했을 때:

```text
Door=OPEN
ControllerState=3
Alarm=1
Pump=0
Heater=0
```

을 확인했습니다.

시험 조건과 실제 관찰 결과를 분리하고, 요구한 상태에서 시험이 수행된 경우에만 HW VERIFIED로 기록했습니다.

**결과: HW VERIFIED**

---

## 7. OLED I2C Black Screen Investigation

### 증상

OLED를 연결하고 firmware를 실행했지만 화면이 켜지지 않았습니다.

### 원인 추적

OLED 주소와 HAL 설정만 확인하는 수준에서 끝내지 않고 I2C bus와 GPIO까지 단계적으로 확인했습니다.

```text
Black screen
↓
0x3C / 0x3D probe
↓
HAL full-address scan
→ 0 devices
↓
GPIO bit-bang full scan
→ 0 devices
↓
PB8 / PB9 LOW/HIGH self-test
→ PASS
↓
firmware execution 확인
```

MCU GPIO 제어와 firmware 실행 자체는 확인됐지만 OLED 장치의 ACK는 얻지 못했습니다.

### 판단

확보한 근거만으로 OLED module 자체의 고장을 확정하지 않고 module / power / interface 측 문제 가능성으로 범위를 좁혔습니다.

OLED 기능은 장비의 핵심 제어 경로와 분리하고 Production Firmware를 기존 기준선으로 복구했습니다.

**결과: HW investigation 기록**

---

## 8. Stepper / ULN2003 Hardware Investigation

### 증상

Stepper Driver와 독립 Bench Firmware의 Host 검증 및 UART 시험 경로는 동작했지만 실제 모터가 회전하지 않았습니다.

### 원인 추적

Software command path와 실제 Driver board 표시를 나누어 확인했습니다.

```text
Host test PASS
UART START / DONE PASS
실제 Motor 회전 FAIL
ULN2003 A LED only OFF
```

PC0 / PC1 출력 연결을 교환한 뒤에도 A channel의 LED만 동작하지 않는 현상이 유지됐습니다.

### 판단

현상은 Software Sequence보다 ULN2003 / Motor / Physical channel 측으로 범위가 좁혀졌지만, 실제 고장 부품까지 확정할 근거는 확보하지 않았습니다.

Stepper는 Equipment Controller의 핵심 Pump / Heater / Safety 기능과 분리해 별도 Hardware investigation으로 관리했습니다.

**결과: Hardware investigation 기록**

---

## Verification Principle

이 프로젝트에서는 구현, Host Test, 실제 Hardware Verification을 구분합니다.

```text
IMPLEMENTED
→ 코드 구현 완료

BUILD VERIFIED
→ Build 성공

HOST VERIFIED
→ CTest / regression test 통과

HW VERIFIED
→ 실제 NUCLEO-F411RE 및 센서·출력에서 동작 확인
```

문제가 발생했을 때도 가설을 즉시 원인으로 확정하지 않고 **재현 → 계층 분리 → 원인 추적 → 수정 → 회귀 테스트 → 실물 확인** 순서로 검증했습니다.
