# Communication Safety Phase 2 — Linux Runtime Recovery

**HOST VERIFIED / HARDWARE E2E NOT YET VERIFIED**

Baseline: `7daa741`, 작업 시작 시 Working Tree clean.
Linux Runtime에서 UART 장애 및 Sensor 수신 중단을 기존 Core CommunicationFault에 연결했습니다.
STM32 Firmware, CubeMX, ASCII wire format, Stepper, README는 변경하지 않았습니다.

## 구현과 정책

`apps/controller/controller_runtime.cpp/.hpp`는 기존 main의 UART 처리 책임을 모은 작은 Runtime입니다.
실제 SerialTransport / ASCII parser·serializer / EquipmentController를 재사용합니다.
Clock만 주입 가능하여 Host에서 timeout과 재연결 경계를 제어합니다.

| 조건 | 동작 |
|---|---|
| 초기 시작 | `communication_ok=false`, START 거부, 성공적으로 열린 포트에 안전 출력부터 송신 |
| 최초 정상 Line 전 | 정상 통신으로 간주하지 않음; 100 ms마다 안전 complete refresh 송신 |
| 최초 연결 후 1000 ms 무수신 | CommunicationFault, 안전 출력 시도, 포트 닫기 및 재연결 대기 |
| UART open/read/write 실패 | 프로세스 유지, 통신 무효화, Core Fault 반영, 포트 닫기 |
| 마지막 정상 Sensor 수신 후 1000 ms 경과 | Sensor stale Fault, 안전 출력 시도 후 포트 닫기 |
| 포트가 닫힌 상태 | 실패/닫기 완료 후 500 ms 간격으로 동일 경로의 `openPort()` 재시도 |
| 재연결 성공 | 통신 유효성 false 유지, 안전 complete refresh 우선 송신; ERROR 해제 없음 |
| 정상 Sensor 재수신 | 통신 유효성만 복구; 기존 ERROR/Alarm 및 안전 출력 유지 |
| RESET | 기존 Core RESET 조건 만족 시에만 Idle 복귀; 자동 RESET/START 없음 |
| SIGINT/SIGTERM | 루프 종료, 기존 STOP 처리 및 안전 출력 best-effort 송신, 포트 닫기 |

Core 변경은 `Idle + communication_ok=false`에도 기존 `enterError(CommunicationFault)`를 호출하는 것과
통신 무효 상태의 START 거부 두 가지입니다. Ready/Run의 E-Stop 우선순위를 유지합니다.
이미 Error 상태인 경우 기존 Alarm을 덮어쓰지 않습니다. 예를 들어 EmergencyStop 중 통신이 끊기면
`communication_ok=false`가 되지만 Alarm은 EmergencyStop으로 남습니다. RESET 조건은 수정하지 않았습니다.

CommunicationFault의 기존 출력은 Pump OFF, Heater OFF, Valve CLOSED, RUN OFF,
ERROR LED ON, Buzzer ON입니다. 최초 정상 연결의 대기 상태는 기존 Idle 안전 출력입니다.
포트 오류로 명령을 전송할 수 없으면 물리 출력 차단은 STM32 Phase 1에 의존합니다.

## 시간 및 Sensor 정의

- `SensorTimeout = 1000 ms`: STM32의 100 ms Sensor 보고 약 10주기와 Linux/USB 지연 여유를 고려한 초기값.
- `ReconnectInterval = 500 ms`: 동일 포트 재시도 간격. main은 10 ms sleep하여 busy loop 방지.
- `SafeRefreshInterval = 100 ms`: 초기/재연결 후 Sensor를 기다리는 동안 안전 출력 유지.
- 기준 Clock은 `steady_clock`. 경과 시간이 timeout **이상**이면 수신 처리보다 먼저 Fault로 전환.
- 성공적으로 열린 시각을 첫 정상 Line의 대기 deadline 기준으로 사용하되, 이 시각 자체가 통신 유효성을 뜻하지 않음.
- 기존 `AsciiProtocol::parseSensorLine()`이 성공한 완성 Line만 freshness 갱신.
  Unknown, malformed, NaN 등 parser 거부값, incomplete Line, 단순 byte 수신은 갱신하지 않음.
- 동일 read 묶음은 수신 시각을 공유. 송신 지연 이후 남은 오래된 Line으로 deadline을 다시 연장하지 않음.
- Transport read 한 번은 최대 4096 byte로 제한하여 연속 RX/noise가 Runtime 검사를 무기한 점유하지 않도록 함.
- 기존 writeAll의 1000 ms deadline은 매 write 시도 전에 검사하여 EINTR 반복도 예산을 따르도록 함.

1000 ms는 소프트웨어 판정 기준입니다. 실제 감지/출력/종료 시간에는 Linux scheduling,
Runtime 처리 및 최대 1000 ms의 write 대기가 추가될 수 있습니다. Hard real-time/실측 latency 보장은 아닙니다.

이 단계의 stale은 **Sensor stream 전체의 liveness**입니다. TEMP/PRESSURE/DOOR/ESTOP/MOTOR 중
어느 정상 Line이든 수신되면 갱신됩니다. 개별 필드별 stale 검사, 모든 필드의 새 snapshot 보장은 없습니다.
재연결 시 기존 Sensor 값은 유지하고 communication_ok만 무효화하므로, RESET의 다른 조건들은
수신 갱신된 값과 기존 cached 값에 대해 기존 방식으로 평가됩니다.

## STM32 Phase 1과 안전 재동기화

출력은 항상 기존 serializer의 순서와 형식으로 송신합니다:

```text
PUMP → HEATER → VALVE → BUZZER → RUN_LED → ERROR_LED
```

초기 연결과 재연결의 첫 송신은 Pump/Heater/RUN OFF, Valve CLOSED로 강제합니다.
Buzzer/ERROR는 기존 Core 상태를 따릅니다. 안전 묶음 앞에 LF 하나를 보내 이전 MCU partial Line의 경계를 마칩니다.
새 command나 wire field를 추가하지 않습니다. 초기·재연결 안전 송신이 실패하면 Sensor 처리를 진행하지 않고 다시 닫습니다.
출력 재전송 queue는 없으며 실패 전의 ON snapshot과 실패한 RX 묶음의 나머지를 재사용하지 않습니다.

`openPort()`는 기존 closePort 경로로 application RX buffer/discard 상태를 초기화하고,
`tcflush(TCIFLUSH)`로 OS에 남은 이전 RX도 폐기합니다. 따라서 open 전 도착한 Sensor Line은 정상 초기 입력으로 인정하지 않습니다.
이후 주기 보고를 새로 받아야 합니다.

ACK/Session/Sequence가 없으므로 write 성공은 STM32의 safe resynchronization 완료 확인이 아닙니다.
일부 정상 Line의 stale replay, USB/driver 내부 queue의 원격 생성 시각은 판별하지 못합니다.
STM32의 250 ms complete-refresh 조건 충족 여부와 실제 latch 해제는 Hardware E2E에서 확인해야 합니다.

## 지속 실행과 운전 명령

main의 10초 제한을 제거했고 SIGINT/SIGTERM까지 실행합니다. UART 오류도 자동 종료 사유가 아닙니다.
종료 신호 처리기는 플래그만 변경하며 I/O와 Core 호출은 foreground에서 수행합니다.
SIGKILL/프로세스 crash에서는 종료 송신이 보장되지 않으므로 STM32 독립 fail-safe가 필요합니다.

기존 실행 파일에 없던 START/RESET 입력 인터페이스는 추가하지 않았습니다.
이번 Host 검증은 Core API를 직접 호출하여 RUN 중 장애, RESET 거부/허용 및 자동 재개 방지를 검증합니다.
운영 실행 파일에서 사용자가 RESET/START를 전달할 인터페이스는 후속 범위입니다.
재연결 경로는 실행 인자로 받은 동일 경로이며 장치가 다른 이름으로 재등장하는 경우 자동 검색하지 않습니다.

## Host 검증

실행한 명령:

```sh
cmake -S . -B build-refactor
cmake --build build-refactor -j2
ctest --test-dir build-refactor -R LINUX_RUNTIME_RECOVERY_HOST --output-on-failure
ctest --test-dir build-refactor --output-on-failure
```

| 검증 | 결과 |
|---|---|
| Linux Build 및 등록된 Host Test Build | PASS |
| 전체 CTest | **41/41 PASS** — 기존 40 + 신규 1 |
| `LINUX_RUNTIME_RECOVERY_HOST` | PASS |
| 기존 STM32 Communication Fail-safe / Stepper Host Test | PASS |
| Hardware Flash / 실제 USB·모터·출력 시험 | 미실행 |

신규 Suite는 실제 SerialTransport/Runtime/Core/serializer와 PTY를 사용합니다.
Clock 주입으로 경계를 결정하고, link-time write wrapper로 실제 Runtime의 write 오류 분기를 결정적으로 검증합니다.

- 최초 정상 Line 전 START 거부, 첫 안전 complete refresh, 최초 무수신 timeout.
- RUN 중 정상 Line에 의한 deadline 갱신, T−1/T 경계, 무수신/unknown/malformed/incomplete stale.
- 실제 PTY 단절 read 실패, 경로 재등장, 500 ms 이전 재시도 없음, 재연결 첫 출력의 안전성.
- write EIO 후 Fault, 남은 RX 묶음 폐기, 이전 ON 재전송 없음.
- 지연된 write 이후 같은 read 묶음의 오래된 Line으로 freshness 복원 방지.
- 재연결 전 OS RX queue 및 partial Line 폐기.
- 정상 Sensor 회복 후에도 Error 유지, 자동 START 거부, 기존 RESET guard 유지.
- 초기 포트 없음에서 복구, 기존 E-Stop Alarm 유지, RUN에서 종료 시 안전 송신.

기존 TEST_27은 연결 시 첫 안전 묶음을 확인한 뒤 정상 Sensor 5개에 대한 출력 5개를 검사하도록 갱신했습니다.
TEST_28은 open 전 입력 대신 첫 안전 송신 후 Sensor를 보냅니다. Sensor stale이 발생해도
10초 이후까지 프로세스가 유지되고 SIGTERM으로 종료코드 0을 반환하는지 확인합니다.
기존 Core/Protocol/Transport baseline 출력 테스트는 통과했습니다.

## 남은 Hardware E2E

1. Production STM32와 연결하여 최초 안전 묶음 및 정상 Sensor 수신 확인.
2. 실제 운전 상태에서 USB/VCP 단절·write 실패·송신 중단별 Linux CommunicationFault와 물리 출력 차단 확인.
3. MCU 전원 유지 상태에서 Phase 1 latch와 Linux 재연결 안전 묶음의 상호 동작 확인.
4. 같은 장치 경로로 재등장한 뒤 이전 ON 미재전송 및 Error/안전 출력 유지 확인.
5. 향후 명시적 운전 입력 경로에서 RESET guard와 별도 START 요구 확인.
6. Sensor timeout·재연결 간격·250 ms refresh 전달 시간·USB jitter·backpressure의 실제 시간 측정.

이번 Host 결과를 Hardware PASS로 확대하지 않습니다. 기존 STM32 command-loss HW VERIFIED 기록과
정확한 timeout latency 미계측 상태는 그대로 유지합니다.
