# 공정 단계화 / Cycle re-arm / Critical Sensor Safety — 2026-10-12

**Host 45/45 PASS / STM32 Debug 및 기존 HW target build PASS / 이번 정책의 실물 검증 대기**.
작업 전 기준선은 44/44 PASS였다. commit/push/flash는 수행하지 않았다.
기존 작업 트리 변경과 `run` 스크립트를 보존했다.

## 최종 동작

- UART communication 정상 + Core Idle이면 STATE:IDLE/Blue를 전송한다.
  초기 READY 확인 5초 동안 Blue, 완료 후 READY/Yellow이다.
  communication invalid이면 SAFE, 기존 Error latch는 Red 보존 정책을 유지한다.
- READY는 fresh Door/E-Stop 수신 확인, Door CLOSED, E-Stop OFF, communication OK,
  Core Idle/sequence 없음 상태가 연속 5초 유지되어야 한다. Door OPEN, E-Stop,
  communication fault/reconnect에서 timer를 취소한다. 완료 전 START는 거부한다.
- RX 센서 line은 즉시 interlock 및 해당 센서의 critical 판정을 수행한다.
  sequence 전환은 **한 poll에서 최대 한 번**이다. START가 들어온 RX poll에서는
  CheckDoor를 유지하고 다음 poll에서 진행한다. stdin도 동일 Runtime 명령 경로를 사용한다.
- CheckDoor → PumpOn(진입 시 Pump ON) → CheckPressure → HeaterOn(진입 시 Heater ON)
  → CheckTemperature → ProcessStart → Run 순서이다.
- CheckPressure는 `pressure_valid && 200 < raw < 3900`일 때만 통과한다.
  CheckTemperature는 `temperature_valid && 5 < °C < 50`일 때만 통과한다.
  ProcessStart에서도 temperature 유효성/범위를 다시 검사한다. 만료되었으면
  CheckTemperature로 돌아가며 RUN에 진입하지 않는다.
- 별도 공정 목표 압력/온도가 제공되지 않아 위 critical 정상 범위를 준비 조건으로 적용했다.
  인위적인 단계별 시간 지연은 추가하지 않았다. 이미 센서 조건이 충족되었다면
  기존 약 10ms poll마다 다음 단계로 진행하므로 준비 단계가 빠르게 완료될 수 있다.
- RUN 진입 시 steady_clock timer를 시작한다. 29.999초에는 Run,
  30.000초 이후 첫 poll에서 Pump/Heater/Valve/RUN output OFF 및 Idle/Blue로 전환한다.
  ProcessComplete → Shutdown → None의 후처리 표시는 이후 poll마다 한 단계씩 정리한다.
  그동안 출력은 OFF이다. 준비에 소비한 시간은 RUN 30초에 포함하지 않는다.
- 정상 종료 시 Rearm=WaitOpen이다. Door가 계속 CLOSED이면 READY로 돌아가지 않는다.
  종료 **이후 새 Door OPEN**을 받아 WaitClose, 이후 CLOSED를 받아 Armed가 되어야
  새 5초 qualification을 시작한다. 자동 시작은 없다.
- 정상 종료 re-arm과 Error RESET은 구분한다. Error RESET 성공 시 Armed/Idle/Blue에서
  새 5초 확인을 수행한다. re-arm 도중 통신을 잃으면 미완료 Door 이력도 폐기한다.
- START/STOP/RESET은 stdin/UART 모두 대소문자를 구분하지 않는다.
- 1초 STATUS의 기존 필드는 유지하고 온도/습도를 소수점 한 자리로 표시한다.
  `Rearm=0` Armed, `1` WaitOpen, `2` WaitClose를 디버깅용으로 추가했다.

## Critical 판정과 RESET

판정은 START 이후 준비(Core Ready) 및 Run에서 수행한다. Idle/READY 표시 대기 상태의
측정은 운전 중의 2회 판정에 이월하지 않는다.

| 센서 | Low critical | High critical | AlarmCode |
|---|---|---|---|
| Temperature | ≤5.0°C | ≥50.0°C | TemperatureCritical (7) |
| Humidity | ≤15.0% | ≥90.0% | HumidityCritical (8) |
| Pressure raw | ≤200 | ≥3900 | PressureCritical (9) |

- 같은 센서의 같은 방향(low 또는 high)이 **새 유효 측정 2회 연속** 확인되면 Error이다.
  poll/update 횟수나 다른 센서 line으로 재평가한 cached 숫자는 새 측정으로 세지 않는다.
- 정상 값, INVALID/잘못된 값, freshness 만료는 해당 센서 streak을 끊는다.
  low→high 변경도 새로운 streak 1회로 시작한다. 새 START와 성공한 RESET은 counters를 초기화한다.
- 기존 DHT 5초 freshness를 유지하고 Pressure는 새 `pressure_valid`와 1초 freshness를 사용한다.
  ADC UART 정수 범위는 0..4095이며 비정상 범위/파싱 실패/INVALID는 validity를 해제한다.
  invalid 또는 stale 값은 준비 조건이나 critical 판정을 통과시키지 않는다.
- 유효 critical 1회가 준비 조건에 걸리면 해당 Check 단계에서 대기하고, 같은 critical의
  두 번째 유효 측정이면 Error로 전환한다. unavailable 값 자체를 critical 숫자로 대체하지 않는다.
- Error 출력은 기존 물리 E-Stop과 동일하게 Pump/Heater/RUN OFF, Red, buzzer 약 2초이다.
  buzzer가 꺼지거나 센서 값이 정상 복귀해도 Error/alarm latch는 유지된다.
- 물리 E-Stop, 운전 중 Door OPEN, communication loss, motor fault는 기존 즉시 검사한다.
  이전 PressureFault/OverTemperature의 단발 숫자 판정은 요청한 2회 critical 정책으로 대체했다.
  기존 AlarmCode 숫자 0..6은 유지하고 새 원인만 7..9로 추가했다.
- RESET은 Error에서만 허용한다. fresh Door/E-Stop, CLOSED/OFF, communication OK,
  motor fault 없음과 **현재 유효한 세 센서가 모두 표의 critical 범위 밖**이어야 한다.
  stale/invalid 측정 또는 아직 critical인 값으로는 reset할 수 없다.
  정상값으로 돌아온 뒤 B1 PC13 또는 기존 RESET 입력으로 해제한다.

6-line control refresh 순서, STATE 명령, MCU ON replay 차단 및 command-loss latch 코드는
이번 작업에서 변경하지 않았다. 새로운 fault는 기존 Error 출력 경로를 사용한다.

## 검증

- 작업 전: 전체 build / CTest **44/44 PASS**.
- 최종: 전체 build / CTest **45/45 PASS**, 10.60초. 기존 44개 모두 포함한다.
- 신규 `LINUX_CYCLE_SAFETY_HOST`: 한 RX batch/한 poll 한 단계, START iteration 유지,
  실제 pressure 대기, invalid/stale temperature 대기와 ProcessStart 재검사,
  준비 시간 제외 RUN 29.999/30.000초, 종료 후 출력 OFF 및 단계별 후처리,
  Door OPEN→CLOSED 재확인/5초 qualification, 6개 critical 경계와 안쪽 정상 경계,
  새 측정만 2회 계산, invalid/정상/반대 방향/stale의 streak reset, 준비 단계 fault,
  ERROR latch/출력/2초 buzzer/RESET guard, 대소문자 명령, E-Stop/Door 회귀를 검증했다.
- 기존 READY/recovery/DHT/STATUS/failsafe 및 GPIO Host tests 모두 PASS.
  기존 Core fixtures는 기본 숫자 대신 명시적인 유효 측정값을 주도록 수정했다.
  baseline의 변경은 PumpOn/HeaterOn entry 출력, 완료 시 즉시 OFF/Idle, 새 critical 원인에 한정했다.
- STM32 Debug / Dht11Hardware / StepperHardware build 확인 PASS (이번 변경에 따른 MCU 재컴파일 없음).
- `git diff --check` PASS. CubeMX main.c의 기존 CRLF를 보존하면서 검사하도록 `.gitattributes`에
  해당 파일만 `cr-at-eol`을 지정했다. 다른 whitespace 검사는 유지한다.

## 이번 작업의 변경 파일

- `apps/controller/controller_runtime.hpp`, `controller_runtime.cpp`, `main.cpp`
- `modules/core/include/alarm_code.hpp`, `command.hpp`, `equipment_controller.hpp`, `sensor_state.hpp`
- `modules/core/src/equipment_controller.cpp`, `modules/protocol/src/ascii_protocol.cpp`
- `tests/CMakeLists.txt`, `tests/baseline_output.txt`
- `tests/core/test_sensor_state.hpp` (신규), `test_controller.cpp`, `test_safe_valve.cpp`
- `tests/logging/test_file_logging.cpp`, `tests/firmware/test_io_runtime.cpp`
- `tests/transport/test_runtime_cycle.cpp` (신규), `runtime_fixture.hpp`,
  `test_runtime_policy.cpp`, `test_runtime_recovery.cpp`, `test_runtime_environment.cpp`
- `.gitattributes` (신규)
- 이 문서, `docs/CONTROLLER_RUNTIME_POLICY.md`, `docs/DHT11_PRODUCTION_PATH.md`,
  `firmware/stm32-equipment-io/HARDWARE_VERIFICATION.md`

## 실제 HW에서 다시 확인할 항목

아래 항목은 이번 정책 변경본에서 **아직 HW 미검증**이다.

1. `./run` 연결 후 Idle Blue, qualification 동안 Blue/OFF, 5초 완료 Yellow 및 조기 START 거부.
2. `start` 입력 후 단계별 Pump/Heater 출력과 pressure/실제 temperature 조건 대기.
3. DHT 분리/무효/만료 때 CheckTemperature 유지, 정상 측정 복구 시에만 RUN 진입.
4. RUN entry부터 30초, 정상 종료 시 OFF/Idle Blue, 문을 닫아 두어도 READY 재진입 없음.
5. 종료 후 Door OPEN→CLOSED 및 새로운 5초 확인을 거쳐야 다음 START 허용.
6. 각 센서 low/high 첫 유효 측정은 유지, 동일 두 번째 측정은 해당 AlarmCode/Error/Red/OFF.
7. critical Error 후 buzzer 약 2초, Red/latch 유지, 정상 복귀만으로 해제되지 않음.
8. unsafe/invalid/stale RESET 거부, 안전 측정과 E-Stop release 후 B1 RESET 및 새 READY 확인.
9. 각 준비 단계 및 RUN의 E-Stop/Door interlock, command-loss OFF/ERROR red 보존과 UART recovery.
10. 1초 STATUS 한 자리 소수 및 START/STOP/RESET 대소문자 입력 확인.

사용자가 제공한 이전 DHT11 실물 관찰은 TEMP/HUMIDITY UART 수신과 Linux 실제값 표시 두 항목에
한해 별도 기록했다. 이번 sequence/critical/re-arm의 HW VERIFIED로 확대하지 않았다.

권장 commit message (미실행):
`feat: stage process cycles with door rearm and confirmed sensor critical faults`
