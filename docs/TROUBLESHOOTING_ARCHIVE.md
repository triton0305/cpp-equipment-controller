# Equipment Controller — Troubleshooting Master Report

> 기준: 2026-10-12~13 및 관련 개발·회의·배선·검증 대화  
> 목적: 프로젝트 전체에서 실제로 발생했거나 검증 과정에서 확인된 문제를 중복 제거하여 `증상 → 원인 추적 → 조치 → 검증 → 현재 상태` 기준으로 통합 정리  
> 원칙:
> - 같은 원인/같은 사건이 여러 대화에서 반복된 경우 하나로 통합한다.
> - 증상이 비슷하더라도 원인이 다르면 별도 사건으로 유지한다.
> - `HOST VERIFIED`, `HW VERIFIED`, `DEFERRED`, `UNVERIFIED`, `미해결`을 구분한다.
> - 실제 검증 범위를 넘어선 과장은 금지한다.
> - 실패한 접근과 검증 절차상의 오류도 트러블슈팅 이력에 포함한다.

---

# 1. C++ Controller / Runtime / Protocol 실제 문제

## 1.1 Host Build에서 `command.hpp` 미검출

### 증상
Host C++ 빌드에서 다음과 같은 include 관련 fatal error가 발생했다.

```text
command.hpp: No such file or directory
```

### 원인
Host target에 프로젝트 `include/` 경로가 제대로 연결되지 않았다.

### 조치
루트 CMake에서 다음과 같이 include path를 target에 추가했다.

```cmake
target_include_directories(equipment_controller PRIVATE include)
```

### 검증
Host Build 및 Run 성공.

### 현재 상태
**해결 완료**

---

## 1.2 `EquipmentState`, `SequenceStep`, `setSensorState()` 컴파일 오류

### 증상
다음과 같은 C++ 컴파일 오류가 발생했다.

```text
‘EquipmentState’ does not name a type
‘SequenceStep’ does not name a type
no declaration matches ‘EquipmentController::setSensorState(...)’
```

### 원인
enum 타입 및 멤버 함수 선언/정의 간 정합성이 맞지 않았다.

### 조치
누락된 enum 및 메서드 declaration/definition을 정리했다.

### 검증
Host Build/Run PASS.

### 현재 상태
**해결 완료**

---

## 1.3 Door OPEN 상태 처리 정책 오류

### 증상
Door가 열려 있다는 이유만으로 IDLE 상태에서도 즉시 ERROR로 진입하는 구조가 나타났다.

### 실제 요구사항
- IDLE + Door OPEN: START 거부
- READY / RUN + Door OPEN: Fault
- Door OPEN 자체가 IDLE에서 곧바로 ERROR를 의미하지는 않음

### 원인
Door safety와 운전 상태 정책이 분리되지 않았다.

### 조치
Door interlock 정책을 상태별로 구분했다.

### 검증
Door Open START reject, RUN 중 Door Fault 등을 Host Test로 확인.

### 현재 상태
**해결 완료**

---

## 1.4 E-Stop Polarity Logic Bug

### 증상
정상 상태에서도 EmergencyStop Alarm이 발생했다.

### 원인
E-Stop 상태를 판단하는 Boolean 조건이 반대로 구현되어 있었다.

잘못된 형태:

```cpp
if (!sensor_state_.emergency_stop)
```

실제 의도:

```cpp
if (sensor_state_.emergency_stop)
```

### 조치
조건을 올바른 polarity에 맞게 수정.

### 검증
- 정상 sequence 복구
- E-Stop Fault test PASS
- 이후 Physical E-Stop E2E까지 PASS

### 현재 상태
**해결 완료 / 회귀 검증 PASS**

---

## 1.5 `communication_ok=false`인데 RUN 유지

### 증상
Communication fault를 주입했는데도 Controller가 RUN 상태와 actuator ON 상태를 유지하는 실패가 확인됐다.

예:

```text
state=RUN
alarm=0
pump=1
heater=1
```

### 원인
초기 Controller에서 CommunicationFault 경로가 충분히 구현되지 않았다.

### 조치
후속 개발에서 `CommunicationFault` 처리 및 안전 상태 전이를 추가했다.

### 검증
후속 기준선에서 Alarm 6 Communication Fault PASS 기록 존재.

### 현재 상태
**후속 기준선에서 해결**
단, 정확한 단일 패치 시점은 별도 추적이 필요하다.

---

## 1.6 Pump 동작 전 Pressure Fault 가능성

### 문제
Pump가 아직 켜지지 않은 시점에 낮은 압력값만으로 PressureFault가 발생할 수 있었다.

### 원인
Pressure Fault 조건이 actuator 상태와 분리되어 있었다.

### 조치
Pressure fault를 `pump_on` 조건과 연결했다.

### 검증
정상 sequence Host Test PASS.

### 현재 상태
**해결 완료**

---

## 1.7 `enterError()` 이후 로직 계속 실행 가능

### 문제
Fault 진입 후에도 남은 sequence logic이 실행되면 Alarm overwrite나 추가 상태 변경이 가능했다.

### 조치
Fault 진입 직후 `return`하도록 정리.

### 현재 상태
**해결 완료**

---

## 1.8 `closePort()` 중복 정의 Compile Error

### 증상
`serial_transport.cpp` 안에서 `closePort()`가 두 번 정의되어 빌드 실패.

### 원인
기존 함수를 교체하지 않고 새 구현을 추가하면서 동일 멤버 함수가 중복 정의됨.

### 조치
중복 정의를 제거하고 하나의 구현만 유지.

### 검증

```text
[100%] Built target equipment_controller
```

TEST 23 PASS.

### 현재 상태
**해결 완료**

---

## 1.9 Oversized UART Line 처리

### 문제
ASCII line protocol에서 newline이 오랫동안 들어오지 않으면 RX buffer가 계속 증가할 수 있었다.

### 위험
- 메모리 증가
- 이후 정상 message 오염
- 장시간 운전 안정성 저하

### 해결
예:

```cpp
static constexpr std::size_t MAX_LINE_LENGTH = 256;
bool discarding_line_ = false;
```

제한 초과 시:
1. 현재 buffer 삭제
2. `discarding_line_ = true`
3. 다음 newline까지 전부 폐기
4. 이후 정상 line부터 재개

### 검증

```text
[TEST 24] UART oversized line protection
Oversized input | complete lines=0
Recovery | complete lines=1 | line=TEMP:30.5
```

### 현재 상태
**TEST 24 PASS**

---

## 1.10 `disconnect_master` 중복 선언 Compile Error

### 증상

```text
error: redeclaration of ‘const int disconnect_master’
```

### 원인
동일 `main()` scope에 TEST 25 block이 중복 삽입됨.

### 조치
중복 TEST block 삭제.

### 검증
Build PASS 후 TEST 25 실행 성공.

### 현재 상태
**해결 완료**

---

## 1.11 UART Peer Disconnect 미감지

### 증상

```text
[TEST 25] UART peer disconnect
Before disconnect | isOpen=1
After disconnect | read_ok=1 | isOpen=1 | received_bytes=0
```

상대 PTY master를 닫았는데도 연결 상태로 유지됨.

### 원인
기존 `readAvailable()`에서:

```cpp
if (result == 0)
{
    return true;
}
```

로 처리해 `read()==0`을 정상 idle처럼 취급했다.

Nonblocking TTY 환경에서 단순히 `read()==0`만으로 idle과 disconnect를 구분하기 어려웠다.

### 조치
`poll()` 기반 FD 상태를 추가했다.

```cpp
if (poll_result > 0 &&
    (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)))
{
    closePort();
    return false;
}
```

### 검증

```text
After disconnect | read_ok=0 | isOpen=0 | received_bytes=0
```

### 현재 상태
**TEST 25 PASS**

---

## 1.12 Disconnect 이후 동일 Transport 객체 재연결

### 문제
Disconnect 이후:
- FD 상태
- `rx_buffer_`
- `discarding_line_`

등이 남으면 동일 객체 재사용이 깨질 수 있었다.

### 조치
`closePort()`에서 RX session state를 초기화.

### 검증

```text
[TEST 26] UART reconnect after disconnect
First connection | isOpen=1
After disconnect | read_ok=0 | isOpen=0
Reconnect | opened=1 | isOpen=1
After reconnect | ... | line=TEMP:31.5
```

### 현재 상태
**TEST 26 PASS**

---

## 1.13 Linux Runtime이 UART 오류에서 종료되는 구조

### 문제
기존 runtime은 UART open/read/write 오류가 발생하면 프로그램 자체가 종료될 수 있었고 실행 시간도 약 10초 수준이었다.

### 목표
장시간 운전 기준:

```text
통신 이상 감지
→ 안전 상태
→ reconnect
→ safe resync
```

### 조치
Persistent Runtime + CommunicationFault + reconnect 정책 추가.

### 현재 상태
**HOST VERIFIED**

---

## 1.14 Sensor Stale Detection

### 문제
UART port 자체는 살아 있어도 정상 sensor data가 더 이상 들어오지 않을 수 있다.

### 조치
정상 Sensor line 기준으로 1000 ms freshness timeout 추가.

Malformed, incomplete, unknown line은 freshness를 갱신하지 않음.

### 현재 상태
**HOST VERIFIED**

---

## 1.15 통신 복구 후 기존 actuator 자동 재적용 위험

### 문제
Reconnect 직후 기존 `Pump ON`, `Heater ON` 상태를 다시 내보내면 자동 재가동 위험이 있다.

### 조치

```text
Reconnect
→ Safe complete refresh
→ Communication recovery
→ ERROR 자동 해제 X
→ RUN 자동 복귀 X
→ 이전 ON 자동 재전송 X
```

RESET과 새로운 START가 필요하도록 유지.

### 현재 상태
**HOST VERIFIED**

---

## 1.16 `start` 명령이 먹히지 않음

### 증상
터미널에:

```text
start
```

를 입력해도 반응이 없었다.

### 원인
CLI parser가 대문자 `START`, `STOP`, `RESET`만 인식.

### 조치
Case-insensitive parsing으로 변경.

### 검증

```text
START
Start
start
sTaRt
```

모두 Host Test.

### 현재 상태
**해결 완료**

---

## 1.17 START 직후 Sequence가 RUN까지 급속 진행

### 증상

```text
START
→ READY
→ RUN
→ Pump ON
→ Heater ON
```

이 거의 즉시 진행됨.

심지어 당시:

```text
Temperature=unavailable
```

인데도 RUN까지 진입.

### 초기 가설
Core switch/case가 여러 단계를 한 번에 진행하는 것으로 의심.

### 최종 원인
STM32가 한 RX batch에 여러 sensor line을 보낼 때 **sensor line 하나를 처리할 때마다 sequence를 한 단계씩 advance**하고 있었다.

예:

```text
DOOR
ESTOP
PRESSURE
TEMP
HUMIDITY
```

가 한꺼번에 들어오면:

```text
CheckDoor
→ PumpOn
→ CheckPressure
→ HeaterOn
→ CheckTemperature
→ ProcessStart
→ RUN
```

이 빠르게 진행될 수 있었다.

### 조치

```text
Sensor RX
→ 즉시 safety/interlock 검사만 수행

Poll
→ sequence transition 최대 1회
```

정책으로 분리.

### 검증
`LINUX_CYCLE_SAFETY_HOST` 추가.

CTest:

```text
44/44
→ 45/45 PASS
```

### 현재 상태
**해결 완료 / HOST VERIFIED**

---

## 1.18 RUN 종료 후 자동 READY 재진입

### 문제
RUN 30초 종료 후 Door가 계속 CLOSED라면 다시 READY qualification이 시작될 수 있었다.

### 사용자 정책
새 Cycle 전에는 실제 Door OPEN/CLOSE 과정이 있어야 함.

### 조치
Cycle re-arm 상태 추가:

```text
Armed
WaitOpen
WaitClose
```

정상 종료 후:

```text
RUN 종료
→ WaitOpen
→ Door OPEN
→ WaitClose
→ Door CLOSED
→ Armed
→ 5초 qualification
→ READY
```

### 검증
Host 45/45 PASS 및 HW 확인.

### 현재 상태
**해결 완료**

---

## 1.19 Critical Sensor Noise 단발 ERROR 방지

### 문제
단일 noisy sample로 장비가 ERROR에 들어갈 수 있었다.

### 조치
동일 센서 + 동일 방향 critical 값이 **새 유효 측정 2회 연속**일 때만 ERROR.

다음은 count 안 함:
- cached 값
- 다른 sensor line
- poll 반복

다음은 streak 끊음:
- 정상값
- INVALID
- stale
- low→high 방향 변화

### 현재 상태
**HOST VERIFIED**

---

## 1.20 RESET과 Sensor Recovery 역할 분리

### 정책
Critical 발생 후 sensor가 정상으로 돌아와도:

```text
자동 ERROR 해제 X
자동 재시작 X
```

### RESET Guard
ERROR 상태에서:

```text
Door CLOSED
E-Stop OFF
Communication OK
Motor fault 없음
Temperature safe
Humidity safe
Pressure safe
```

조건 충족 후에만 RESET.

### 검증
B1 RESET 실제 동작 확인.

### 현재 상태
**해결 완료**

---

## 1.21 `STATE` Protocol 추가 후 Regression 4개 실패

### 증상

```text
37/41 PASS
```

실패:
- TEST_27
- STM32_RUNTIME_HOST
- STM32_COMMUNICATION_FAILSAFE_HOST
- LINUX_RUNTIME_RECOVERY_HOST

### 원인
기존 6-line control packet:

```text
PUMP
HEATER
VALVE
BUZZER
RUN_LED
ERROR_LED
```

만 기대하던 test/protocol에 `STATE`가 추가됨.

또 기존 fail-safe는 이 정확한 6-line ordering을 사용하고 있었다.

### 조치
`STATE`는 6-line refresh **뒤의 별도 command**로 분리.

```text
STATE:SAFE
STATE:IDLE
STATE:READY
STATE:RUN
STATE:ERROR
```

### 검증
전체 regression PASS 복구.

### 현재 상태
**해결 완료**

---

## 1.22 Command-loss 시 ERROR Red 보존

### 위험
Communication safe-state의 `STATE:SAFE`가 기존 Fault의 Error Red까지 끌 수 있었다.

### 조치
이미 ERROR latch 상태인 경우 Red를 유지하도록 정책 및 test 수정.

### 현재 상태
**해결 완료**

---

## 1.23 IDLE Blue 미점등

### 증상
IDLE 상태에서 Blue가 처음 보이지 않는 현상.

### 조사
Stale executable / STATE path 변경 시기와 겹쳐 정확한 단일 원인을 독립적으로 확정하지 못함.

### 조치
STATE command path와 communication-valid 상태 표시를 정리.

### 검증
실물에서:

```text
IDLE Blue
READY Yellow
RUN Green
```

확인.

### 현재 상태
**최종 동작 해결 / 단일 Root Cause 미확정**

---

## 1.24 `./run` 직후 E-Stop Alarm 일시 발생

### 증상
RESET 후 한 번:

```text
./run 실행 직후 E-Stop Alarm
```

발생.

재실행 후 정상.

### 판정
재현되지 않아 원인 확정하지 않음.

### 현재 상태
**Transient / Non-reproducible**

---

## 1.25 UART Reconnect Startup False Alarm 6

### 증상
정상 종료 후:

```bash
Ctrl+C
./run
```

재실행하면:

```text
Invalid SENSOR line: 6
Invalid SENSOR line: OOR:OPEN

EVENT sensor communication recovered
EVENT Controller state=3 alarm=6
Sensor stale; communication invalid
```

발생.

### 분석
UART stream 중간에서 reader가 붙으면 원래:

```text
PRESSURE:2756\n
DOOR:OPEN\n
```

이던 stream이:

```text
6\n
OOR:OPEN\n
```

처럼 첫 line fragment로 들어올 수 있다.

그 뒤 일부 정상 sensor만 받은 상태에서 recovery를 너무 일찍 선언하고 stale watchdog가 동작하여 Communication Error가 false latch된다.

### 필요한 수정

#### A. 첫 partial line 폐기

```text
Reconnect
→ 첫 newline까지 discard
→ 이후 complete line부터 parser 전달
```

#### B. Recovery barrier

필수 fresh sensor set 확보 전에는 communication recovery 선언 금지.

#### C. Startup/recovery 중 stale watchdog 억제
Fresh set 완성 전에는 operational stale fault 금지.

#### D. 실제 운전 중 communication-loss safety는 그대로 유지

### 현재 상태
**미해결 / 다음 개발 최우선 Bug**

---

# 2. Communication Safety

## 2.1 통신 상실 시 마지막 GPIO 출력 유지 위험

### 문제

```text
Pump / Heater ON
→ Linux ↔ STM32 통신 상실
→ Linux가 오류를 알아도 OFF command 전달 불가
→ STM32가 마지막 GPIO 상태 유지 가능
```

### 원인
STM32 runtime에 command freshness 감시가 없었다.

### 조치
STM32 자체 Command-loss Fail-safe 추가.

### 현재 상태
**HW VERIFIED**

---

## 2.2 아무 UART 명령이나 Watchdog Refresh로 오인 가능

### 문제
예:

```text
PUMP ON
이후 RUN_LED만 계속 수신
```

해도 전체 통신 정상으로 오인할 수 있다.

### 조치
다음 6개 output command가 정확한 순서로 완성된 경우만 complete refresh로 인정.

```text
PUMP
HEATER
VALVE
BUZZER
RUN_LED
ERROR_LED
```

추가 조건:
- PUMP부터 ERROR_LED까지 250 ms 이내
- 누락/순서오류/중복/malformed/unknown/incomplete/UART error는 refresh 불인정

### 현재 상태
**해결 완료**

---

## 2.3 Timeout 이후 Stale `ON` 재활성화 위험

### 문제

```text
Pump ON
→ Timeout
→ Pump OFF
→ 늦게 도착한 PUMP:ON
→ Pump 다시 ON
```

가능.

### 조치
Timeout Latch 도입.

```text
Timeout
→ Pump OFF
→ Heater OFF
→ RUN LED OFF
→ failsafe latch ON
```

Latch 상태에서는 위험 ON 차단.

Safe complete refresh를 받아야 latch 해제.

### 현재 상태
**해결 완료**

---

## 2.4 Safe Refresh와 ON 재활성화 분리

### 문제
Safe refresh 하나로 latch 해제와 이전 ON 적용을 동시에 하면 복구 순간 재가동 가능.

### 조치

```text
Safe refresh
→ latch 해제
→ 여전히 OFF

다음 별도 complete refresh
→ ON 허용
```

### 현재 상태
**해결 완료**

---

## 2.5 RUN LED Cache와 실제 GPIO 불일치

### 문제
Timeout으로 실제 Green GPIO만 끄고 `run_led=true`가 남으면 후속 indicator update에서 Green이 다시 켜질 수 있었다.

### 조치

```cpp
run_led = false;
```

까지 communication safe-state에 포함.

### 현재 상태
**해결 완료**

---

## 2.6 Timeout 경계 시 stale ON 회피

### 위험

```text
t = timeout 경계
PUMP:ON 수신
```

에서 RX를 먼저 처리하면 stale session이 살아날 수 있다.

### 조치
`IoRuntime_Poll()` 순서:

```text
Timeout 검사
→ RX 처리
```

### 검증

```text
T - 1 정상
T timeout
T + 1 timeout
```

### 현재 상태
**해결 완료**

---

## 2.7 1000ms Timeout의 의미

### 구현값

```text
IO_CONTROL_TIMEOUT_MS = 1000
IO_CONTROL_REFRESH_WINDOW_MS = 250
```

### 주의
이 값은 기능상 동작을 검증한 것이지 안전 인증된 정량 latency는 아니다.

### 판정

```text
Functional physical behavior = HW VERIFIED
Exact 1000ms response time   = NOT INSTRUMENT-MEASURED
```

---

## 2.8 HW Fail-safe Test Script 자체의 오류

### 증상
정상 ON 단계에서 Pump/Heater/RUN이 켜지지 않음.

### 스크립트

```bash
send_safe
sleep 2
send_on
```

### 원인
Timeout은 1초인데 2초 대기하여 Safe sync 직후 다시 timeout/latch가 발생.

### 조치

```bash
sleep 0.2
```

로 변경.

### 결과
사용자가 실제로:

```text
2. NORMAL ON       약 5초 ON
6. ON AFTER RESYNC 약 5초 ON
그 외 OFF
```

확인.

### 현재 상태
**Test Script Bug 해결 / Firmware 정상**

---

## 2.9 USB/VCP 실제 detach 시 열린 FD 오류

### 시험
Windows에서:

```powershell
usbipd detach --busid 3-4
```

### WSL 결과

```text
bash: printf: write error: Input/output error
/dev/ttyACM0 사라짐
```

재attach 후 device 재생성 확인.

### 범위
확인한 것:
- Device disappearance
- Existing FD I/O error
- Device reappearance

미확인:
- Linux Application automatic reconnect Physical E2E

### 현재 상태
**OS/Device 동작 확인 / Auto reconnect HW E2E 미검증**

---

# 3. Build / 실행 기준선 문제

## 3.1 ARM Toolchain 미설치

### 증상
WSL에서 `arm-none-eabi-gcc` 없음.

### 조치
GNU ARM Toolchain 설치.

### 검증
STM32 firmware CMake build 성공.

### 현재 상태
**해결 완료**

---

## 3.2 STM32 CMake Preset 실행 위치 오류

### 증상

```text
CMake Error: Could not read presets from
/home/kccistc/projects/cpp-equipment-controller
```

### 원인
`CMakePresets.json`은 repository root가 아니라:

```text
firmware/stm32-equipment-io
```

기준.

### 조치

```bash
cd ~/projects/cpp-equipment-controller/firmware/stm32-equipment-io
cmake --preset Debug
cmake --build --preset Debug
```

### 현재 상태
**해결 완료**

---

## 3.3 Build 실패 후 Stale Binary 실행

### 문제

```bash
cmake --build ...
./build/equipment_controller
```

를 별개로 실행하면 build가 실패해도 과거 executable이 실행될 수 있었다.

### 조치

```bash
cmake --build build -j$(nproc) && ./build/equipment_controller
```

사용.

### 현재 상태
**해결 완료**

---

## 3.4 `./run`이 오래된 `build-refactor` 실행

### 증상
최신 READY 5초, STATUS 1초 등이 보이지 않음.

기존:

```bash
exec "$ROOT/build-refactor/apps/controller/equipment_controller" "$DEVICE"
```

최신 binary:

```text
build/apps/controller/equipment_controller
```

### 조치

```bash
sed -i \
's|build-refactor/apps/controller/equipment_controller|build/apps/controller/equipment_controller|' \
run
```

### 결과
최신 Runtime 기준 HW 검증 가능.

### 현재 상태
**해결 완료**

---

## 3.5 READY 5초 미동작처럼 보인 현상

### 조사
실제 코드에:

```cpp
static constexpr auto ReadyDelay = std::chrono::seconds(5);
```

존재.

### 실물 결과
Door CLOSED 후 약 5.02초 뒤 READY.

Door OPEN 시 qualification 취소.

### 결론
기능 누락이 아니라 stale executable/시험 방식 혼선.

### 현재 상태
**HW 확인**

---

## 3.6 STATUS 1초 미적용처럼 보인 현상

### 원인
동일하게 오래된 executable 영향.

### 최신 코드
1초 snapshot throttle 존재.

### 결과
최신 build 실행 후 약 1초 간격 확인.

### 현재 상태
**해결 완료**

---

## 3.7 `device_io.c` CMake Target 누락

### 문제
Source file은 있는데 Firmware binary에 link되지 않을 위험.

### 조치
STM32 target에 source 연결.

### 검증
STM32 전체 Build PASS.

### 관련 Commit

```text
19f1e4f
build: include stm32 device io in firmware
```

### 현재 상태
**해결 완료**

---

## 3.8 CubeMX Toolchain EWARM

### 문제
CubeMX Toolchain IDE가 EWARM으로 설정되어 WSL GCC/CMake 개발 흐름과 불일치.

### 조치
CMake 기반으로 설정.

### 현재 상태
**해결 완료**

---

## 3.9 Clock 문서 HSE 8MHz vs 실제 HSI 16MHz

### 실제 generated config
- HSI 16 MHz
- PLL
- SYSCLK 84 MHz
- APB1 Timer 84 MHz
- TIM5 PSC=83 → 1 MHz timer tick

### 원인
문서와 generated configuration 불일치.

### 조치
실제 generated code 기준으로 문서 정정.

### 현재 상태
**해결 완료**

---

## 3.10 최신 DHT11 Source인데 Production에서 TEMP/HUMIDITY 없음

### 증상
Host source는 최신인데 raw UART에는:

```text
DOOR
ESTOP
PRESSURE
```

만 있고 TEMP/HUMIDITY 없음.

### 조사
File timestamp:

```text
Debug ELF 최신
Debug BIN 과거
```

Board에는 과거 production firmware가 남아 있었음.

### 조치
최신 Production ELF를 보드에 Flash.

### 결과

```text
TEMP:21.3
HUMIDITY:46.2
```

raw UART 수신.

Linux Runtime에서도 실측 표시.

### 현재 상태
**Production DHT11 E2E HW 확인**

---

## 3.11 이전 DHT11 Harness가 Board Flash에 남을 가능성

### 위험
`Build`만 했다고 MCU Flash가 자동으로 Production으로 돌아오는 것은 아니다.

### 조치
Communication Fail-safe HW Test 전에 Production Debug preset을 명시적으로 Build/Flash.

### 현재 상태
**시험 기준선 확보**

---

# 4. Windows / WSL / ST-LINK / Serial 환경

## 4.1 Codex 환경에서 ST-LINK 미검출

### 증상

```text
No ST-Link detected!
```

또한:

```text
/dev/bus/usb 없음
/sys/bus/usb/devices 없음
```

### 원인
Codex 실행환경에 사용자의 물리 USB device가 전달되지 않음.

### 결론

```text
STM32 문제 X
Firmware 문제 X
ST-LINK 고장 X
Codex USB access 제한
```

### 현재 상태
**환경 제한 확인 / Host와 HW 검증 분리**

---

## 4.2 Windows NUCLEO를 WSL에서 사용하기 위한 USB passthrough

### 증상
Windows에서는 장치가 보이지만 WSL에는 없음.

### 조치
Windows 관리자 PowerShell:

```powershell
usbipd bind --busid 3-4
usbipd attach --wsl --busid 3-4
```

WSL:

```bash
lsusb
ls -l /dev/ttyACM*
```

### 결과

```text
0483:3752 STMicroelectronics ST-LINK/V2.1
/dev/ttyACM0
```

### 현재 상태
**해결 완료**

---

## 4.3 USB 재연결 후 WSL Attach Session 해제

### 증상
보드를 뺐다 꽂은 뒤 Windows에서는 Shared인데 WSL에서는 root hub만 보임.

### 원인
bind는 남았지만 attach session이 끊김.

### 조치

```powershell
usbipd attach --wsl --busid 3-4
```

재실행.

### 결과
VCP 복구 후 telemetry 수신.

### 현재 상태
**해결 완료**

---

## 4.4 `<BUSID>` Placeholder를 실제로 입력

### 증상
PowerShell ParserError.

### 원인
`<BUSID>` 예제 placeholder를 문자 그대로 입력.

### 조치

```powershell
usbipd attach --wsl --busid 3-4
```

### 현재 상태
**해결 완료**

---

## 4.5 WSL에서 `usbipd` 실행

### 증상

```text
Command 'usbipd' not found
```

### 원인
사용 중인 `usbipd-win`은 Windows 측 도구.

### 조치
역할 분리:

```text
Windows: usbipd list/attach
WSL: lsusb / ttyACM / programmer
```

### 현재 상태
**해결 완료**

---

## 4.6 이미 Attach된 장치 재attach

### 증상

```text
Device with busid '3-4' is already attached to a client.
```

### 판정
장애가 아니라 이미 성공 상태.

### 현재 상태
**해석 수정**

---

## 4.7 WSL Flash Utility 없음

### 증상

```bash
which STM32_Programmer_CLI
which st-flash
which openocd
```

없음.

### 조치

```bash
sudo apt install -y stlink-tools
```

### 현재 상태
**해결 완료**

---

## 4.8 `st-info` USB Access Denied

### 증상

```text
Could not open USB device ...
access error
Found 0 stlink programmers
```

### 원인
일반 사용자 libusb 권한 부족.

### 조치

```bash
sudo st-info --probe
```

### 결과

```text
Found 1 stlink programmers
descr: stm32f411re
```

### 현재 상태
**보드 문제 아님 / Linux permission 문제 확인**

---

## 4.9 `/dev/ttyACM0` EACCES

### 증상

```text
Failed to open serial port: /dev/ttyACM0
```

`strace`:

```text
openat(... /dev/ttyACM0 ...) = -1 EACCES
```

### 조치
- dialout group 추가
- 실제 시험 중 임시:

```bash
sudo chmod 666 /dev/ttyACM0
```

### 결과

```text
READ_OK
WRITE_OK
```

### 현재 상태
**시험 진행 가능**
단, `chmod 666`은 임시 대응이고 영구 환경에는 udev rule 등이 더 적절.

---

## 4.10 `cat /dev/ttyACM0`가 멈춘 것처럼 보임

### 원인
데이터를 기다리는 blocking read의 정상 동작.

### 조치

```bash
timeout 2 cat /dev/ttyACM0
```

또는 실제 runtime 사용.

### 현재 상태
**오해 해결**

---

## 4.11 `st-util`과 `st-flash` 자원 충돌

### 증상

```text
unable to claim
probably already in use
```

### 원인
`st-util`/GDB가 ST-Link를 점유.

### 조치

```bash
pkill st-util 2>/dev/null || true
pkill gdb-multiarch 2>/dev/null || true
```

### 현재 상태
**해결 완료**

---

## 4.12 Programmer Shell 변수 소실

### 증상

```text
Command '' not found
```

### 원인
이전 shell에서 설정한 `DHT11_PROGRAMMER`가 현재 shell에 없음.

### 조치
STM32CubeProgrammer 절대경로 사용.

### 현재 상태
**해결 완료**

---

## 4.13 `<firmware.elf>` Placeholder 문제

### 증상

```bash
arm-none-eabi-objcopy -O binary <firmware.elf> firmware.bin
```

→ 파일 없음.

### 원인
`< >`는 shell redirection이고 placeholder를 실제로 입력함.

### 조치
실제 ELF path 사용.

### 현재 상태
**해결 완료**

---

## 4.14 외부 USB-UART Module 필요 여부 오해

### 상황
처음 외장 USB-UART가 필요하다고 생각했으나 사용자는 그런 모듈이 없다고 지적.

### 확인
NUCLEO ST-LINK에는 USART2 VCP가 존재.

```text
PA2 TX
PA3 RX
115200 8N1
```

### 현재 상태
**구조 정정**

---

# 5. Git / Test Infrastructure / 검증 절차

## 5.1 `top"` 이상 파일 stage

### 문제
E-Stop commit 과정에서 의도하지 않은 `top"` 파일이 stage/commit에 포함.

### 조치
삭제 후 amend.

### 현재 상태
**해결 완료**

---

## 5.2 `equipment_controller.log` untracked

### 원인
Logger 실행 artifact를 ignore하지 않음.

### 조치

```gitignore
*.log
```

### 현재 상태
**해결 완료**

---

## 5.3 DHT11 Test Git Path 오류

### 증상

```text
fatal: pathspec ... did not match any files
```

### 원인
Test가 Firmware 하위가 아니라 repository root `tests/firmware/`에 존재.

### 조치
`git status --short` 실제 경로 기준으로 stage.

### 현재 상태
**해결 완료**

---

## 5.4 Commit Message 일부가 파일명으로 생성

### 증상

```text
tm32): support DHT11 humidity decimals and verify hardware"
```

파일 발생.

### 조치
정확한 파일명 확인 후 삭제.

### 현재 상태
**해결 완료**

---

## 5.5 GitHub Repository moved

### 메시지

```text
This repository moved.
Please use the new location:
https://github.com/triton0305/equipment-controller.git
```

### 조치

```bash
git remote set-url origin https://github.com/triton0305/equipment-controller.git
```

### 현재 상태
**해결 완료**

---

## 5.6 Commit 주기가 너무 큼

### 사용자 지적

> commit 을 이렇게 더디게 하면 끊어서 하는 이유가 없지

### 문제
검증된 기능 A를 commit하지 않은 상태에서 기능 B로 넘어가면 rollback 기준점이 약해진다.

### 정책 변경

```text
작은 기능 구현
→ Build/Run
→ 독립 PASS
→ Commit
→ 다음 기능
```

### 현재 상태
**개발 프로세스 개선**

---

## 5.7 Runtime/Test/Build 변경 혼합

### 문제
서로 성격이 다른 변경을 `git add .`로 한 commit에 넣을 위험.

### 분리
예:

```text
feat: integrate controller UART runtime
test: expand controller runtime coverage
build: export compile commands
```

### 현재 상태
**해결 완료**

---

## 5.8 과거 검증 HEAD와 현재 Source HEAD 불일치

### 문제
과거 문서 기준선은 `f1cc18d`인데 현재 repo는 다른 HEAD와 미커밋 변경을 가짐.

### 조치

```bash
git status
git log
```

및 현재 source 기준 별도 build/CTest 재검증.

### 현재 상태
**해결 완료**

---

## 5.9 `git diff --stat`만으로 변경량 판단

### 문제
Untracked file은 기본 diff에 나타나지 않음.

### 조치
`git status`, `wc -l`, staging 기준으로 확인.

### 현재 상태
**해결 완료**

---

## 5.10 `git diff --check`와 Untracked

### 문제
Untracked file은 `git diff --check` 검사 대상이 아니다.

### 조치
Stage 후:

```bash
git diff --cached --check
```

사용.

### 현재 상태
**해결 완료**

---

## 5.11 CubeMX `main.c` CRLF

### 증상

```text
trailing whitespace
^M
```

처럼 표시.

### 원인
실제 trailing space가 아니라 CRLF의 carriage return.

### 조치
일회성:

```bash
git -c core.whitespace=cr-at-eol diff --cached --check
```

후속 repository 정책:

```gitattributes
firmware/stm32-equipment-io/Core/Src/main.c whitespace=blank-at-eol,blank-at-eof,space-before-tab,cr-at-eol
```

### 현재 상태
**해결 완료**

---

## 5.12 `main.cpp` 첫 줄 불필요 Blank Line

### 조치
삭제 후 amend.

### 현재 상태
**해결 완료**

---

## 5.13 `Testing/` CTest 산출물 Commit 후보 혼입

### 증상

```text
?? Testing/
```

### 조치

```bash
rm -rf Testing/
```

### 현재 상태
**해결 완료**

---

## 5.14 `RUNTIME.md` 첫 문서 Commit에서 누락

### 원인
두 문서 중 `HARDWARE_VERIFICATION.md`만 stage.

### 조치
두 문서를 다시 stage/commit.

### 현재 상태
**해결 완료**

---

## 5.15 검증 문서에 E-Stop PASS와 과거 FAIL 동시 존재

### 문제
최신 결과와 과거 표가 모순.

### 조치
최신 결과는 PASS/HW VERIFIED로 갱신하고 과거 FAIL은:

```text
RESOLVED / SUPERSEDED
```

명시.

### 현재 상태
**해결 완료**

---

## 5.16 Python Verifier → C++ 전환 중 Timeout 누락

### 기존 Python

```python
subprocess.run(..., timeout=15)
```

### C++ 초기 구현 문제
fork/read/waitpid 구조에 timeout 없음.

### 조치
- `steady_clock` deadline
- 15초 초과 시 `SIGKILL`
- `waitpid()` reap

### 현재 상태
**해결 완료**

---

## 5.17 C++ Verifier Pipe Deadlock 가능성

### 문제
Parent가 child 종료를 기다린 뒤 pipe를 읽으면 child output이 pipe buffer를 채워 서로 대기할 수 있음.

### 조치
- Pipe read side O_NONBLOCK
- `poll()`
- 실행 중 stdout/stderr drain
- `waitpid(WNOHANG)`
- timeout 후 `SIGKILL`
- child reap

### 검증

```text
30/30 PASS
```

### 현재 상태
**해결 완료**

---

## 5.18 TEST 27의 검증 범위 과장

### 문제
UART 응답 30줄 후 child를 `SIGKILL`.

따라서 확인하는 것은 UART response이지:
- 자연 종료
- exit code 0
- stderr empty
- lifecycle

까지는 아님.

### 조치
TEST 28 추가.

### 검증
Runtime lifecycle 조건 검증.

### 현재 상태
**해결 완료**

---

## 5.19 File Logging 자동 Regression 부족

### 문제
Console 비교는 있었지만 실제 log file content 자동검증 부족.

### 조치
TEST 29 추가.

### 검증
INFO/WARNING/ERROR, Controller Event, 순서/횟수 검사.

### 현재 상태
**해결 완료**

---

## 5.20 Safe State Valve Assertion 공백

### 문제
Safe State라고 문서화했지만 기존 fault test가 `valve_open == false`를 직접 확인하지 않음.

### 조치
TEST 30 추가.

### 한계
최종 상태 CLOSED는 확인했지만 실제 OPEN→Fault→CLOSE 전이 자체는 별도.

### 현재 상태
**검증 공백 보완**

---

## 5.21 “옛 테스트가 사라졌나?” 문제

### 조사
- Git history
- reflog
- unreachable objects
- shell history

### 결과
정식 TEST 1~26은 현재 tests로 이동돼 보존.

누락된 것은 임시 Python에서만 하던:
- SENSOR accepted count
- Controller state count
- stderr
- exit code

조건.

### 조치
TEST 28에서 복원.

### 현재 상태
**해결 완료**

---

## 5.22 Stepper 제거 후 전체 재검증 시도

### 문제
실제 변경은 Stepper 제거 및 Production Flash 복구뿐인데 기존 전체 시스템을 처음부터 재검증하려고 함.

### 사용자 지적

> 왜 처음부터 검증을 다시함? 걍 보드에서 모터만 뺀 건데

### 개선 정책
1. 변경 범위 확인
2. 기존 HW VERIFIED 유지
3. 영향 범위만 재시험
4. 근거 있을 때만 전체 검증 확대

### 현재 상태
**검증 프로세스 개선**

---

## 5.23 Python 코드를 Bash에 입력

### 증상

```text
bash: syntax error near unexpected token ...
```

### 원인
`ser.write(...)`는 Python 코드.

### 조치
Python heredoc/interactive 환경에서 실행.

### 현재 상태
**해결 완료**

---

## 5.24 저장소 구조를 잘못 가정한 grep

### 잘못된 명령

```bash
grep ... CMakeLists.txt src include
```

### 실제 구조

```text
apps/controller/
modules/core/
modules/transport/
modules/protocol/
modules/logging/
firmware/
tests/
```

### 현재 상태
**절차 오류 수정**

---

## 5.25 Codex 사용 한도 종료

### 상황
작업 중 usage limit 도달.

### 대응
Working Tree를 유지하고 이미 완료된 build/test 결과를 확인한 뒤 수동으로 마무리.

### 현재 상태
**작업 유실 없음**

---

# 6. DHT11 Troubleshooting

## 6.1 반복 `INVALID_DATA`

### 최초 결과

```text
INVALID_DATA 15
OK            1
```

정상 Frame 예:

```text
TEMP=25.7
HUM=46.0
CHECKSUM=VALID
```

Measurement interval:

```text
2100 ms
```

정상.

### 가설 1 — Scheduling
2100ms가 정확해 scheduler 문제 가능성 낮아짐.

### 가설 2 — Pull-up
외부 10kΩ 비교 후 결과가 동일하여 주원인에서 제외.

### Diagnostic Harness
실패 Frame:

```text
DHT11:INVALID_DATA,
REASON=HUMIDITY_DECIMAL,
FRAME=COMPLETE,
BITS=40,
RAW=2D-02-19-09-51,
CHECKSUM_RX=51,
CHECKSUM_CALC=51
```

### RAW 분석

```text
Humidity Integer    = 45
Humidity Decimal    = 2
Temperature Integer = 25
Temperature Decimal = 9
Checksum            = 0x51
```

즉:

```text
45.2 %RH
25.9 °C
Checksum VALID
```

### 근본 원인
Decoder가 Humidity decimal byte를 `0`만 허용.

### 수정

```text
humidity_tenths = raw[0] * 10 + raw[1]
decimal 0~9 허용
```

### Host 검증
문제 Frame 자체를 test vector로 추가.

```text
CTest 37/37 PASS
```

### Hardware 검증

```text
16/16 consecutive OK
INVALID_DATA 0
Temperature 25.8 °C
Humidity 44.3~44.5 %RH
Measurement Interval 2100 ms
```

### 현재 상태
**DHT11 Driver — HW VERIFIED**

---

## 6.2 Pull-up 추가 직후 `RESPONSE_TIMEOUT 16/16`

### 증상
기존에는 Frame까지 오던 센서가 Pull-up 추가 직후 응답 자체가 사라짐.

### 조치
Pull-up 제거 후 기존 배선 복구.

### 결과
이전 `INVALID_DATA + 일부 OK` 상태로 복구.

정확한 Pull-up 구조로 다시 시험:

```text
Baseline: OK 1 / INVALID 15
Pull-up : OK 1 / INVALID 15
```

### 결론
Pull-up 부족은 근본 원인 아님.

최초 timeout의 정확한 물리 원인은 확정하지 않음.

---

## 6.3 DHT11 Production Runtime Integration Risk

### 문제
DHT11 Capture는 timing-sensitive synchronous section을 포함.

Potential impact:
- Door
- E-Stop
- UART RX/TX
- Pressure ADC

latency.

### 초기 대응
Driver를 Hardware Harness로 격리 검증.

### 이후
Production telemetry까지 통합되어 실제 TEMP/HUMIDITY 수신 확인.

### 현재 판정

```text
DHT11 Driver/HW/Production telemetry = 검증
Production 전체 WCET/Safety latency 영향 = 정량 미검증
```

---

## 6.4 DHT11 출력 6자리 소수

### 증상

```text
Temperature=21.300000
Humidity=45.800000
```

### 조치
1자리 formatting.

### 결과

```text
Temperature=23.3
Humidity=50.5
```

### 현재 상태
**해결 완료**

---

# 7. STM32 Peripheral / Wiring / Board Troubleshooting

## 7.1 MCU Pin 이름과 Arduino Header Label 혼동

### 문제
Firmware pin (`PB0`, `PB1` 등)과 NUCLEO Arduino `D/A` label을 혼동.

### 정리 예
- PB0 = A3
- PB1 = D7
- PA6 = D12
- PA7 = D11
- PB4 = D5
- PB5 = D4
- PB6 = D10
- PB7 = Morpho PB7

### 현재 상태
**정리 완료**

---

## 7.2 CubeMX I2C1 Pin Conflict

### 초기 배치
PB8/PB9가 HEATER/BUZZER 등에 사용되어 I2C1 사용 불가.

### 사용자 지적
기존 핀 설계 충돌.

### 조치
- HEATER → PA6
- BUZZER → PA7
- PB8 → I2C1 SCL
- PB9 → I2C1 SDA

### 현재 상태
**해결 완료**

---

## 7.3 PB0/PB1 Pull-up 변경이 안 된 것처럼 보임

### 원인
Pinout만 보고 있었고 실제 GPIO configuration row에서 변경하지 않음.

### 조치
GPIO setting에서 pull-up 적용.

### 현재 상태
**해결 완료**

---

## 7.4 4-pin Tactile Button Leg 파손

### 상황
버튼 한 개의 leg/cap 이상 발견.

### 처리
Safety input에는 spare 정상 button을 우선 사용하도록 판단.

### 현재 상태
**HW 결함 관찰 / 주요 기능은 다른 입력으로 진행**

---

## 7.5 Board RESET Button 고장 의심

### 상황
보드 RESET 버튼이 고장난 것 같다고 보고.

### 대응
검증을:
- power cycle
- flash
- runtime restart

경로로 진행.

### 현재 상태
**별도 Board HW Issue / 핵심 E2E에는 우회 가능**

---

## 7.6 RGB Resistor 구조

### 문제
RGB LED에 resistor 하나만 쓰는 것으로 오해 가능.

### 조치
R/G/B 각 channel마다 resistor.

### 현재 상태
**배선 정책 정리**

---

## 7.7 RGB Common Cathode/Anode

### 관찰
`RGB GND` marking.

### 판단
Common Cathode로 사용.

### 주의
부품 marking 기반 판단이며 전기적 실측까지 포함한 별도 검증은 아님.

---

## 7.8 Buzzer Active/Passive 불명확

### 초기
부품 타입 미확정.

### 실물 결과
Fault 상황에서 실제 audible ON 확인.

### 판정
- Buzzer 기능 출력: **HW VERIFIED**
- 부품 정확한 Active/Passive 타입: **미확정**

---

## 7.9 DC Motor 직접 GPIO 구동 위험

### 문제
DC Motor를 STM32 GPIO/PWM에 직접 연결하면 전류/역기전력 문제가 있음.

### 조치
Driver/MOSFET/Flyback 필요성을 확인하고 DC Motor를 MVP에서 제외.

Stepper + ULN2003 방향으로 변경.

### 현재 상태
**위험 회피**

---

## 7.10 Pressure ADC Blocking 위험

### 조치
ADC read:

```text
start
→ completion polling
→ 10 ms timeout
→ stop
```

### Host Test
- 0
- 2048
- 4095
- timeout
- start failure
- stop failure
- tick wraparound

### HW
가변저항:

```text
0 ~ 4095
175개 distinct intermediate values
```

### 현재 상태
**HW VERIFIED**

---

## 7.11 Pressure Engineering Unit 미정

### 현재 값

```text
PRESSURE:1820
```

등은 Pa/kPa/bar가 아니라 12-bit ADC raw count.

### 현재 상태
**ADC Acquisition HW VERIFIED / Engineering Calibration 미정**

---

# 8. E-Stop / Door Physical Troubleshooting

## 8.1 E-Stop Physical 1차 FAIL

### 증상
사용자가 E-Stop을 눌렀지만 약 380줄 동안:

```text
ESTOP:OFF
```

만 관찰.

PB1도 계속 HIGH.

### 당시 판단
E-Stop Physical Input FAIL.

### 주의
배선/핀 오류 가능성을 추정했지만 확정하지 않음.

### 후속 시험

```text
ESTOP:OFF
→ ESTOP:ON
→ ESTOP:OFF
→ ESTOP:ON
```

확인.

### 최종
**E-Stop Input — HW VERIFIED**

이전 FAIL은 **RESOLVED / SUPERSEDED**, 정확한 원인 미확정.

---

## 8.2 RUN-state E-Stop 시험을 READY에서 너무 일찍 실행

### 목표

```text
RUN → E-Stop
```

### 실제 1차
READY에서 E-Stop을 눌렀음.

기능은 반응했지만 RUN-state E2E로 인정하지 않음.

### 재시험
실제 RUN 로그 확인 후 사용자에게 E-Stop 조작 요청.

### 결과

```text
before=RUN
state=ERROR
alarm=EmergencyStop
Pump OFF
Heater OFF
Buzzer ON
ERROR Red ON
```

### 현재 상태
**RUN-state EmergencyStop Physical E2E — HW VERIFIED**

---

## 8.3 Door Fault Physical E2E

### 실물
RUN 중 Door OPEN:

```text
EVENT Controller state=3 alarm=1
Door=OPEN
Pump=0
Heater=0
```

### 추가 검증
Linux output command와 MCU GPIO snapshot 비교:

```text
296 / 296
```

일치.

### 현재 상태
**Door Physical E2E — HW VERIFIED**

---

## 8.4 Fault 정책과 Test Harness Cleanup 혼동

### 상황
Fault 확인 후 Harness가 3초 뒤 모든 output OFF 수행.

### 위험
Controller 자체가 3초 후 자동으로 Alarm을 끈다고 오해 가능.

### 실제 Controller 정책

```text
Buzzer/Error ON
until RESET
```

### 현재 상태
**문서상 분리 완료**

---

# 9. Stepper Troubleshooting

## 9.1 Stepper 실제 회전 실패

### Software 상태

```text
Stepper Host Test PASS
CTest 40/40 PASS
STM32 Production Build PASS
StepperHardware Build PASS
```

### 실물
모터 회전 확인 실패.

UART:

```text
START FWD 512
DONE COILS_OFF
```

### 결론
명령 수신 및 software sequence 완료는 확인했지만 물리 회전 성공은 아님.

### 현재 상태
**Stepper Software — HOST VERIFIED**
**Stepper Hardware — DEFERRED**

---

## 9.2 ULN2003 IN1/A LED 미점등

### 관찰

```text
A / IN1 = OFF
B / IN2 = 반응
C / IN3 = 반응
D / IN4 = 반응
```

### 5V 전원 변경
B/C/D 밝기 증가, A는 계속 OFF.

### PC0↔PC1 Cross Test
신호선을 바꿔도 A만 계속 OFF.

### 추론
- PC0 software omission 가능성 낮아짐
- ULN2003 IN1/A 접점/채널
- A LED 자체
- 점퍼 접촉

등 우선 의심.

### 주의
직접 전압 측정이 없어 ULN2003 칩 불량 또는 PC0 정상 여부를 확정하지 않음.

### 현재 상태
**Root Cause 미확정**

---

## 9.3 Stepper 전원을 3.3V에서 5V로 변경

### 초기
5V 28BYJ-48를 3.3V 쪽에서 구동.

### 조치
5V로 변경.

### 결과
B/C/D LED 밝기는 증가했지만:
- A LED 미점등 지속
- Motor 회전 미확인

### 결론
전원 부족만이 전체 원인은 아님.

---

## 9.4 Stepper 범위 제외 및 Production 복구

실제 Hardware 원인 미확정 상태에서 성공했다고 처리하지 않고 Stepper 통합을 보류.

Production Firmware를 다시 Build/Flash.

### 현재 상태
**Stepper HW DEFERRED / Production 복구 성공**

---

# 10. OLED Troubleshooting

## 10.1 OLED Black Screen

### Hardware
4핀 I2C:

```text
GND
VCC
SCL
SDA
```

STM32:

```text
PB8 = SCL
PB9 = SDA
100 kHz
```

주소 후보:

```text
0x3C
0x3D
```

### 최초 결과
Build/Flash 성공, 화면은 계속 BLACK.

---

## 10.2 Custom I2C Driver에서 BUSY 의심

Debugger:

```text
oled_status_address = 0x0
oled_status_failed = true
screen = DISABLED
SR1 = 0
SR2 = 0x2
CR1 = 0
errors = 1
```

`SR2=0x2`에서 BUSY 의심.

---

## 10.3 SCL LOW 발견

초기 GPIOB IDR:

```text
PB8 SCL = LOW
PB9 SDA = HIGH
```

SCL wire 분리:

```text
SCL = HIGH
SDA = HIGH
```

wire 교체 후 재연결 상태에서도 idle HIGH/HIGH 유지.

### 의미
초기 물리선 상태 문제를 하나 제거.

---

## 10.4 CubeMX / GPIO Setting 확인

PB8/PB9:

```text
AF4_I2C1
Alternate Function Open Drain
```

Clock 활성화 확인.

초기 `GPIO_NOPULL`, 내부 `GPIO_PULLUP` 시험도 했지만 OLED는 계속 BLACK.

### 결론
내부 Pull-up 단독 문제는 아님.

---

## 10.5 Custom Driver 자체 문제 가능성 제거

HAL API 기반으로 단순화:

```text
HAL_I2C_IsDeviceReady()
HAL_I2C_Master_Transmit()
```

0x3C/0x3D probe.

결과:

```text
address=0
ready=false
HAL_ERROR
```

---

## 10.6 HAL 내부 BUSY

GDB로 HAL 내부 확인.

```text
I2C_WaitOnFlagUntilTimeout(...)
```

에서 BUSY flag가 해제되기를 기다림.

`ErrorCode=0`.

즉 transaction error code보다 bus가 정상 transaction 단계에 진입하지 못하는 상황에 가까웠다.

---

## 10.7 Boot Delay 가설

약 200 ms boot delay + retry.

### 결과
ACK 없음, 화면 변화 없음.

### 결론
Boot timing 원인 가능성 낮아짐.

---

## 10.8 OLED 코드가 실제 실행되는지 검증

### Source
`main.c`:

```text
MX_I2C1_Init()
→ OledStatus_Init(&hi2c1)
→ IoRuntime_Init()
```

### CMake
`OLED_STATUS_DISPLAY=1`.

### compile_commands
`-DOLED_STATUS_DISPLAY=1`.

### ELF objdump
실제 `main()` branch 순서에서 OledStatus_Init 호출 확인.

### 결론
**OLED code가 실행되지 않는 가설 배제**

---

## 10.9 Full 7-bit Address Scan

범위:

```text
0x03 ~ 0x77
```

### 결과

```text
oled_scan_first_address = 0
oled_scan_device_count = 0
```

즉 I2C bus에서 ACK device 0개.

---

## 10.10 GDB 변수를 너무 일찍 읽은 측정 실수

한 번은 MCU가 `HAL_Delay(500)` 안에 있을 때 diagnostic variable을 읽음.

### 문제
Scan 완료 전 값을 결과처럼 해석할 수 없음.

### 조치

```text
continue
→ 2~3초 실행
→ break
→ 변수 확인
```

### 재결과
여전히 device_count=0.

### 현재 상태
**검증 절차 오류 수정**

---

## 10.11 HAL 독립 GPIO Bit-bang I2C

### 목적
HAL/I2C peripheral 설정 자체를 배제.

### 구조

```text
I2C1 disable
→ PB8/PB9 GPIO Open Drain
→ START/CLOCK/DATA 직접 생성
→ ACK 직접 판독
```

0x03~0x77 scan.

0x3C/0x3D ACK 시 SSD1306 full pixel test.

### 결과
ACK 0.

---

## 10.12 Bit-bang Code가 ELF에 실제 포함됐는지 확인

objdump로 확인:
- DWT enable
- CYCCNT
- 0x03~0x77 loop
- 0x3C/0x3D compare
- address<<1
- START/STOP
- write_byte
- SDA ACK read

### 결론
조건부 compile/optimizer로 code가 빠진 가설 배제.

---

## 10.13 I2C Bus Recovery

### 추가
- SCL pulse ×9
- STOP
- Slow clock 약 50us half-cycle
- DWT CYCCNT explicit reset

### 결과
여전히 ACK 0.

---

## 10.14 GPIO Self-Test

### 진단 변수

```text
oled_diag_scl_low_ok
oled_diag_scl_high_ok
oled_diag_sda_low_ok
oled_diag_sda_high_ok
```

### 결과

```text
true
true
true
true
```

### 의미
MCU는 PB8/PB9를 실제 LOW/HIGH로 drive하고 readback 가능.

---

## 10.15 OLED 최종 판정

### 확인됨

```text
OLED code build            PASS
OLED init call             PASS
ELF 포함                   PASS
PB8/PB9 control            PASS
GPIO readback              PASS
HAL 0x3C/0x3D probe        FAIL
HAL full scan              0 devices
GPIO bit-bang full scan    0 devices
OLED display               FAIL
```

### 확인하지 못함

```text
OLED VCC/GND 실측
다른 OLED module 교체
Logic analyzer / oscilloscope
정확한 controller IC identification
```

### 정확한 결론

```text
Software diagnostic verified
I2C device response failed
OLED HW verification failed
Root cause not conclusively isolated
```

### 현재 상태
**OLED HW Verification FAILED / Root Cause 미확정**

---

## 10.16 OLED 실험 후 Production 복구

실험 변경:
- CMakeLists
- main.c
- HAL MSP
- `oled_status.*`

등을 원복.

Production Firmware Build/Flash:

```text
Flash written and verified! jolly good!
```

Git clean 확인.

### 현재 상태
**Production 기준선 복구 완료**

---

# 11. 기타 설계/정책 이슈

## 11.1 Critical Safety Threshold와 Process Progression Target 혼동

### Critical Safety

```text
Temperature <= 5°C or >= 50°C
Humidity    <= 15% or >= 90%
Pressure    <= 200 or >= 3900
```

### 문제
이 값의 반대 범위를:

```text
200 < Pressure < 3900
5 < Temperature < 50
```

공정 진행 조건으로 그대로 사용하면 정상 센서값이 시작부터 조건을 만족한다.

### 의미
Critical threshold는:

> 위험 범위를 벗어났는가?

를 판단하는 값.

Process progression target은:

> 다음 공정 단계로 넘어가도 되는가?

를 판단하는 값.

둘은 다르다.

### 현재 상태

```text
Critical Safety Threshold = 확정
Process Progression Target = 미확정 / TODO
```

---

## 11.2 Production CLI START Path 부재

### 상황
Controller Core에는 START API가 있지만 당시 Production executable이 자동 START/외부 START path를 충분히 제공하지 않음.

### Physical E2E
임시 Harness가 기존 Controller API를 호출하여 START 수행.

### 판정
Controller Core 결함이라기보다 CLI/Runtime entry 기능 공백.

### 현재 상태
**후속 개발 대상**

---

# 12. 최종 상태 요약

## 12.1 해결 및 검증 완료

- Host Build include/configuration 오류
- enum/declaration compile 오류
- Door 정책
- E-Stop polarity
- Pressure fault gating
- Fault return handling
- UART line fragmentation / oversized line
- UART peer disconnect detection
- SerialTransport reconnect
- Persistent Linux Runtime
- Sensor stale detection
- Safe resync 정책
- CLI command case-insensitive
- Sequence one-step-per-poll
- Cycle re-arm
- Critical 2-sample confirmation
- RESET guard
- STATE protocol regression
- Command-loss Error Red preservation
- READY qualification
- 1초 STATUS
- STM32 command-loss fail-safe
- Stale ON latch protection
- Timeout boundary handling
- DHT11 decimal decoder
- DHT11 Hardware/Production telemetry
- Pressure ADC
- Door Physical E2E
- E-Stop Physical E2E
- Pump/Heater/Buzzer/RGB 주요 physical outputs
- STM32 Flash / VCP / WSL 환경
- Git/CTest/Verifier 주요 infrastructure 문제

---

## 12.2 해결하지 못했거나 의도적으로 보류

| 항목 | 현재 판정 |
|---|---|
| UART reconnect startup false Alarm 6 | **미해결 / 최우선 Bug** |
| Process progression target | **미확정 / TODO** |
| Stepper physical rotation | **DEFERRED** |
| ULN2003 A channel root cause | **미확정** |
| OLED display / I2C ACK | **HW FAILED / Root Cause 미확정** |
| Pressure engineering-unit calibration | **미정** |
| 정확한 Communication Fail-safe latency | **정량 미계측** |
| Production 전체 Safety WCET | **정량 미계측** |
| OLED controller/module 고장 여부 | **미확정** |
| 일시적 startup E-Stop Alarm | **미재현** |
| Production CLI START path | **후속 개발 대상** |

---

# 13. 포트폴리오 / 자기소개서 활용 우선순위

## 1순위 — Communication-loss Fail-safe + Recovery

핵심 흐름:

```text
Linux 통신 상실
→ 상위 Controller가 OFF를 보낼 수 없는 구조적 한계 발견
→ STM32에 command freshness / timeout / local safe-state 구현
→ ordered complete refresh
→ timeout latch
→ stale ON 차단
→ safe resync
→ 실제 물리 출력 차단 HW 검증
```

보여주는 역량:
- 안전 제어 아키텍처
- 장애 모드 분석
- Linux/MCU 책임 분리
- Fail-safe 설계
- Physical verification

---

## 2순위 — DHT11 `INVALID_DATA` Root Cause Analysis

핵심 흐름:

```text
INVALID_DATA 반복
→ 2100ms scheduling 정상
→ Pull-up 전후 결과 동일
→ Diagnostic Harness
→ FRAME=COMPLETE
→ BITS=40
→ Checksum VALID
→ HUMIDITY_DECIMAL만 실패
→ Decoder validation bug 특정
→ 수정
→ Host 37/37
→ HW 16/16 OK
```

보여주는 역량:
- Raw protocol 분석
- 가설 제거
- HW/SW 경계 진단
- Checksum 활용
- Root cause 기반 수정

---

## 3순위 — Sequence Transition Bug

핵심 흐름:

```text
START 직후 RUN/Pump/Heater
→ 실제 SequenceStep 확인
→ sensor line마다 state advance 발견
→ Safety handling과 sequence transition 분리
→ one-poll-one-step
→ 신규 regression
```

보여주는 역량:
- Event-driven control debugging
- State machine reasoning
- Runtime/Core boundary analysis

---

## 4순위 — UART Disconnect / Reconnect

핵심 흐름:

```text
PTY peer disconnect
→ read()==0 정상 처리 때문에 미감지
→ poll(POLLHUP/POLLERR/POLLNVAL)
→ port close
→ 동일 객체 reopen
→ TEMP:31.5 정상 수신
```

보여주는 역량:
- Linux TTY
- Nonblocking I/O
- Fault detection / recovery
- Automated PTY test

---

## 5순위 — OLED I2C Black Screen 분석

핵심 흐름:

```text
Black Screen
→ HAL probe
→ SCL LOW
→ wiring 정리
→ HAL full scan
→ ACK 0
→ bit-bang full scan
→ ACK 0
→ bus recovery
→ GPIO self-test PASS
→ root cause 미확정
→ 범위 제외 + Production 복구
```

보여주는 역량:
- 계층별 진단
- Peripheral/HAL/Physical layer 분리
- 실패를 과장하지 않는 검증 태도

---

## 6순위 — Stepper HW 실패 분석

핵심 흐름:

```text
Host test PASS
→ UART START/DONE 확인
→ 실제 motor 미회전
→ ULN2003 A only OFF
→ PC0/PC1 cross test
→ root cause 미확정
→ Hardware integration 보류
→ Production 복구
```

보여주는 역량:
- Software success와 physical success 구분
- Cross-test
- Scope management
- 정확한 검증 상태 관리

---

# 14. 현재 최우선 후속 작업

## 14.1 기술 Bug 1순위

**UART reconnect startup synchronization race**

필요 구조:

```text
Reconnect
→ first partial line discard
→ fresh sensor set collection
→ recovery barrier
→ communication_ok
→ operational stale watchdog
```

Regression 최소 항목:
- partial first line
- reconnect in middle of frame
- incomplete fresh set
- fresh set completion
- no false Alarm 6
- actual runtime communication loss
- existing ERROR latch preservation

HW 재현:

```bash
./run
Ctrl+C
./run
Ctrl+C
./run
```

반복 후 Alarm 6 false latch 확인.

---

## 14.2 설계 Task 1순위

**Critical Safety Threshold와 Process Progression Target 분리**

현재 Critical Safety는 확정됐지만 Process target은 아직 미정.

따라서 다음 단계에서는:
- CheckPressure progression target
- CheckTemperature progression target
- 필요 시 Humidity progression policy

를 별도 설계해야 한다.

---

# 15. 최종 결론

Equipment Controller 프로젝트의 트러블슈팅은 단순한 코드 수정이 아니라 다음 패턴으로 진행됐다.

```text
실물 이상 또는 테스트 실패 발견
        ↓
실행 기준선 확인
        ↓
Host/Core 상태 확인
        ↓
Runtime / Protocol 확인
        ↓
UART Raw Stream 확인
        ↓
WSL / USB Transport 확인
        ↓
STM32 Firmware / Flash 확인
        ↓
GPIO / Peripheral / Device 확인
        ↓
Regression Test
        ↓
Physical Re-verification
        ↓
검증 상태 문서화
```

특히 다음 원칙이 프로젝트 전반에서 반복적으로 적용됐다.

1. **명령 처리 성공과 실제 하드웨어 동작 성공을 구분**
2. **Host PASS와 HW VERIFIED를 구분**
3. **실패 원인을 추정만으로 확정하지 않음**
4. **Test Script 문제와 Product Code 문제를 분리**
5. **기존 정상 기준선과 현재 실행 기준선을 항상 확인**
6. **Critical Safety와 Process Logic을 분리**
7. **실험용 Firmware/Harness가 Production 검증에 남지 않도록 원복**
8. **실제 수행한 검증 범위를 넘어 문서에 과장하지 않음**

현재 대부분의 핵심 Runtime/Communication/Safety/DHT11 문제는 해결 및 회귀검증까지 완료되었고, 남아 있는 핵심 기술 이슈는 **UART reconnect startup synchronization race**, 설계 이슈는 **Process Progression Target 정의**다.

Stepper와 OLED는 각각 실제 하드웨어 검증에 실패했지만 원인을 확정하지 못한 상태를 그대로 유지했고, Production 범위에서는 제외 또는 복구 처리했다.
