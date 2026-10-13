# Controller runtime 정책 검증 — 2026-10-12 (Asia/Seoul)

**최신 sequence/re-arm/critical 정책 및 45/45 검증:** [Cycle Safety 정책](CYCLE_SAFETY_POLICY.md).

상태: **HOST PASS / STM32 BUILD PASS / 사용자 보고 3개 HW 항목 PASS**.
사용자가 전달한 IDLE 표시, READY 지연, RUN 유지/자동 Shutdown 관찰 결과를 반영했다.
아래 기록의 HW VERIFIED는 사용자 관찰에 근거하며 에이전트가 직접 측정한 결과는 아니다. 기존
[Hardware Verification](../firmware/stm32-equipment-io/HARDWARE_VERIFICATION.md)의
HW VERIFIED, Stepper 미검증, OLED software 검증 / hardware failure 기록은 변경하지 않았다.
에이전트는 commit / push / flash를 수행하지 않았다.

## 2026-10-13 UART 동기화 후속 정책

open 직후 Synchronizing에서 safe refresh를 유지하고 fresh DOOR → 바로 다음 valid ESTOP
경계가 완성되어야 Operational/communication recovery로 전환한다. 부분 후보는 Core에 노출하지 않는다.
동기화 중 START/RESET은 거부하며 READY를 시작하지 않는다. open 기준 1000 ms deadline은
noise/부분 센서 수신으로 연장되지 않는다. Operational 이후 기존 valid Sensor line liveness와
개별 측정 freshness를 유지한다. DHT INVALID/부재는 동기화 완료를 막지 않는다.
Error latch는 reconnect로 해제하지 않으며 같은 batch의 후속 B1 RESET은 기존 guard로 평가한다.
이번 reconnect 변경은 **HW 미검증**이다. [현재 통신 정책과 회귀 검증](COMMUNICATION_SAFETY_PHASE2.md)을 참조한다.
아래 과거 검증 결과는 당시 기록이며 이번 변경의 HW 증거로 사용하지 않는다.

## 구현과 적용 범위

- 기존 작업 트리의 STATE 프로토콜, Idle E-Stop 검출, buzzer timer 변경을 유지했다.
- READY qualification은 `ControllerRuntime::updateReady` 한 함수에서 판정한다.
  fresh Door/E-Stop, 통신 정상, CLOSED/OFF, Core Idle이 연속 5초 유지되어야 한다.
  정상 종료 이후에는 Door OPEN→CLOSED re-arm을 거쳐야 새 5초 확인을 시작한다.
  READY 표시는 Core Idle과 분리된다. Core Ready는 START 후 준비 sequence를 의미한다.
- Door OPEN, E-Stop, ERROR, timeout/disconnect/reconnect는 진행 시간과 READY를 취소한다.
  통신 오류는 Door/E-Stop 수신 여부도 폐기한다. RESET 후에도 다시 5초가 필요하다.
- 운영 명령은 `ControllerRuntime::handleCommand`로 전달한다. stdin 또는 UART의
  `START`, `STOP`, `RESET` newline 명령을 지원한다. 명령은 센서 freshness를 갱신하지 않는다.
  START는 READY 완료 전 거부한다. 기존 Core를 직접 호출하는 legacy 단위 테스트는
  runtime qualification을 거치지 않으므로 운영 명령 경로로 사용하면 안 된다.
- RUN 진입을 관측한 steady_clock 시각부터 30초를 계산한다. 그동안 Core
  `update(false)`는 interlock 검사와 ProcessRunning 유지를 수행한다. deadline 이후
  첫 poll에서 출력 OFF/Idle로 전환하고, ProcessComplete → Shutdown → None 후처리는 poll별로 진행한다. 실제 시간 정밀도는
  기존 10ms foreground loop 및 OS/UART 지연의 영향을 받는다. blocking sleep은 추가하지 않았다.
- B1 PC13의 기존 `GPIO_MODE_IT_FALLING`/NOPULL 설정을 유지한다. 기존 Core 소스에는
  EXTI15_10 IRQ 활성화/handler가 없다. IRQ 설정을 추가하지 않고 GPIO 입력을 foreground에서
  읽는다. 30ms debounce, press당 1회, 부팅 시 누르고 있으면 release 후 재press가 필요하다.
  MCU는 control valid + error cache 상태에서만 요청을 만든다. 최신 Door/E-Stop/Pressure
  snapshot 뒤에 `RESET\n`을 추가한다. Door OPEN / E-Stop ON이면 요청을 버린다.
  Linux는 Error 상태, fresh Door/E-Stop, 기존 Core reset guard로 다시 검사한다.
- RESET guard: CLOSED, E-Stop OFF, motor fault 없음, 통신 정상,
  유효한 temperature 5..50°C / humidity 15..90% / pressure 200..3900의 엄격한 내부 범위. 거부된 RESET은 이후 자동 replay하지 않는다.
- Buzzer는 약 2초 후 전송 출력만 OFF가 된다. Core Error/alarm/error_led latch는 유지한다.
- 주기 로그는 steady_clock 기준 1초 간격 snapshot이다. 통신/오류/명령 이벤트는 즉시 stderr에
  기록한다. 기존 10ms loop, 센서별 처리와 UART refresh는 유지한다.
- 6-line refresh 순서는 PUMP → HEATER → VALVE → BUZZER → RUN_LED → ERROR_LED 그대로다.
  그 다음 별도 STATE를 붙인다. STATE는 MCU control freshness를 갱신하거나 latch를 해제하지 않는다.
- RGB: IDLE Blue, READY Yellow, RUN Green, ERROR Red, SAFE OFF.
  기존 ERROR cache가 있으면 communication SAFE에도 Red를 보존한다.
  latch 중 READY/RUN은 정상 색을 표시할 수 없다. 재동기화 safe refresh 전 ON replay는 금지한다.
- 별도 Green/Yellow GPIO는 Pump/Heater **indicator**이며 실제 부하 연결을 추가하지 않았다.
  VALVE/Stepper/OLED 기존 정책은 유지했다.

## DHT11 운영 경로 후속 구현

STM32 실제 DHT11 측정 → TEMP/HUMIDITY UART → Linux SensorState → 1초 snapshot 연결을 완료했다.
초기/실패 값은 INVALID, 새 정상값 수신 전 또는 5초 경과 후 snapshot은 unavailable이다.
온습도 validity/freshness가 연결되어 있으며, 후속 Cycle Safety 변경에서 실제 값에 따른 준비 대기와 critical 판정을 추가했다.

[DHT11 운영 경로 구현·검증 보고서](DHT11_PRODUCTION_PATH.md)에 상세 정책과 변경 파일을 기록했다.
후속 전체 CTest는 **44/44 PASS**이며 production Debug / 독립 DHT11·Stepper build가 통과했다.
운영 DHT11 경로의 실물 검증은 아직 수행하지 않았다.

## DHT11 연결 전 검증 결과

| 단계 | 관련 검증 | 전체 CTest |
|---|---|---|
| 변경 전 기준선 | 기존 수동 수정 포함 build | 41/41 PASS |
| READY 5초 정리 | LINUX_RUNTIME_POLICY_HOST, RECOVERY_HOST | 42/42 PASS |
| START gate | READY 이전 거부 / 이후 sequence 시작 | 42/42 PASS |
| RUN 30초 | 준비 시간 제외, 29.999/30.000초, OFF/Idle | 42/42 PASS |
| B1 RESET | MCU debounce/hold/E-Stop, Linux reset guard, buzzer 2초 | 42/42 PASS |
| 로그/RGB 및 최종 | TEST_28 snapshot 계약 갱신 후 재검증 | **42/42 PASS** |

최종 전체 CTest 시간: 10.49초. 최종 Host build warning 없음, `git diff --check` PASS.
TEST_27, STM32_RUNTIME_HOST, STM32_COMMUNICATION_FAILSAFE_HOST,
LINUX_RUNTIME_RECOVERY_HOST 모두 PASS.

새 policy test는 가상 steady_clock + 실제 PTY/UART 경로로 검증한다:
4.999/5.000초, Door 재확인, E-Stop, timeout/reconnect, fresh 상태 부재,
START gate, RUN 29.999/30.000초(잘못된 RX가 있어도 deadline 처리),
Door RUN fault, RESET freshness/guard, 1.999/2.000초 buzzer와 Error 유지.
100Hz 가상 입력이 모두 처리/응답되는 동안 periodic status만 1Hz임을 확인한다.
이는 실물/OS의 최대 응답 지연을 측정한 결과는 아니다.

STM32 production Debug build PASS:
RAM 2,168 B / 128 KB, FLASH 20,872 B / 512 KB.
산출물: `firmware/stm32-equipment-io/build/Debug/stm32-equipment-io.elf`.

```bash
cmake --build build
ctest --test-dir build --output-on-failure
cd firmware/stm32-equipment-io
cmake --preset Debug -DSTEPPER_HARDWARE_TEST=OFF -DDHT11_HARDWARE_TEST=OFF
cmake --build --preset Debug
```

## 사용자 제공 실물 결과 — 2026-10-12 기록

| 항목 | 입력 조건 | 실제 동작 | 측정 시간 | 사용자 판정 / 기록 |
|---|---|---|---|---|
| IDLE indication | 프로그램 실행, UART 연결 | RGB Blue | 측정 없음 | PASS / HW VERIFIED |
| READY delay | Door CLOSED, E-Stop OFF | 5초 후 RGB Yellow | 5.02 s | PASS / HW VERIFIED |
| RUN duration | READY 상태에서 START, 정상 sequence 완료 | RUN Green 유지 후 자동 Shutdown | 30.1 s | PASS / HW VERIFIED |

측정값은 사용자 보고 그대로 보존한다. 측정 방법·오차·정확한 시작/종료 기준,
시험 시각 및 실행 binary 식별 정보는 제공되지 않았다.
따라서 RUN 30.1 s를 정확히 30.000 s 측정으로 바꾸거나 허용 오차를 새로 정하지 않는다.
READY 대기 중 Pump/Heater OFF, Shutdown 후 실제 출력 OFF/IDLE Blue,
START 조기 거부, fresh 센서 폐기, 실제 DHT11 온도 확인은 이 보고로 검증된 것으로 추정하지 않는다.

## 실물 검증 실행 절차

1. 현재 대상 보드와 운영 firmware를 확인하고 위 Debug ELF를 사용한다.
   이번 세션은 build만 수행했으며 flash는 수행하지 않았다.
2. 실제 UART device를 확인한 뒤 repo root에서 실행한다:
   `./build/apps/controller/equipment_controller /dev/ttyACM0`.
   포트가 다르면 실제 경로로 바꾼다. stdin에 `START` 후 Enter로 시작한다.
   RESET의 실물 검증은 stdin 대신 B1을 사용한다.
3. 아래 순서대로 RGB/indicator/buzzer, 콘솔 상태와 timestamp를 함께 기록한다.
   RUN 30초는 START 시각이 아니라 RUN 상태 진입 시각과 종료 시각을 비교한다.
4. command-loss 시험은 RUN 중 Linux controller를 `SIGSTOP`으로 정지시켜
   STM32의 1초 command timeout을 관찰한다. 정상 ERROR 상태에서도 별도로 시행하여
   red 보존을 확인한다. `SIGCONT` 후 Linux sensor timeout/reconnect, safe refresh,
   fresh 센서 수신, B1 RESET, 5초 READY 재확인 순서를 관찰한다.
   단순 정상 종료는 command-loss 시험으로 간주하지 않는다.
5. 결과마다 보드/firmware 식별, 입력 시각, 실제 출력 시각, 관찰 결과, PASS/FAIL을
   기록한다. 실제 관찰 없이 HW VERIFIED로 승격하지 않는다.

| # | 실물 확인 항목 | 이번 변경본 결과 |
|---|---|---|
| 1 | Boot/safe 6-line synchronization, ON replay 없음 | 미실행 |
| 2 | IDLE Blue | **PASS / HW VERIFIED — 사용자 관찰** |
| 3 | fresh Door CLOSED + E-Stop OFF | 미실행 |
| 4 | 5초 전 Blue/OFF, 5초 이후 READY Yellow/OFF | **5.02 s 후 Yellow PASS / HW VERIFIED**; 대기 중 Blue/출력 OFF는 미확인 |
| 5 | START 전 거부, READY 이후 START 수락 | READY 이후 START/sequence 진행 보고됨; 조기 START 거부는 미확인 |
| 6 | Pump Green indicator | 미실행 |
| 7 | Pressure ADC 및 기존 pressure guard | 미실행 |
| 8 | Heater Yellow indicator | 미실행 |
| 9 | 실제 DHT11 temperature check | 운영 경로 구현·Host 검증 완료, **실물 검증 대기** |
| 10 | RUN Green | **PASS / HW VERIFIED — 사용자 관찰** |
| 11 | RUN 진입부터 30초 | **RUN 유지 30.1 s, 사용자 PASS / HW VERIFIED**; 측정 시작/종료 기준 미제공 |
| 12 | 자동 Shutdown/OFF/IDLE Blue | **자동 Shutdown PASS / HW VERIFIED**; 출력 OFF/IDLE Blue는 미확인 |
| 13 | RUN 중 Door OPEN → 즉시 ERROR/OFF/Red | 미실행 |
| 14 | Idle/READY/RUN E-Stop → ERROR/Red/Buzzer 약 2초 | 미실행 |
| 15 | Buzzer OFF 후 Error/Red 유지 | 미실행 |
| 16 | E-Stop release + 안전 조건 + B1 RESET (unsafe 거부 포함) | 미실행 |
| 17 | IDLE 복귀 후 새 5초 READY 확인 | 미실행 |
| 18 | command-loss OFF, stale RUN 제거, 기존 ERROR red 보존, safe 재동기화 | 미실행 |

남은 작업: 운영 DHT11 경로와 나머지 실물 항목 실행 및 관찰 결과 반영.
Stepper/OLED의 기존 상태는 그대로 유지하며 이번 검증 완료 조건을 대신하지 않는다.

권장 commit message (실행하지 않음):
`feat: qualify readiness and time runtime cycles with safe reset and status reporting`

## 변경 파일 구분

이번 작업에서 수정/추가한 파일:

- `apps/controller/controller_runtime.cpp`, `controller_runtime.hpp`, `main.cpp`
- `modules/core/include/equipment_controller.hpp`, `modules/core/src/equipment_controller.cpp`
- `firmware/stm32-equipment-io/App/Inc/device_io.h`
- `firmware/stm32-equipment-io/App/Src/device_io.c`, `io_runtime.c`
- `tests/CMakeLists.txt`, `tests/firmware/test_io_runtime.cpp`
- `tests/transport/test_runtime_lifecycle.cpp`, `test_runtime_recovery.cpp`
- `tests/transport/runtime_fixture.hpp` (기존 recovery fixture 공용화, 새 파일)
- `tests/transport/test_runtime_policy.cpp` (새 파일)
- `docs/CONTROLLER_RUNTIME_POLICY.md` (이 보고서)

`io_protocol.h`, `io_protocol.c`, `test_runtime_uart.cpp`의 작업 전 수동 변경은
그대로 유지했다. 기존 untracked `Testing/`도 삭제하지 않았다.
