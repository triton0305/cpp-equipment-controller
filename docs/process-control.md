# Process Control

기준: 2026-10-13 · `c5cbb81`. READY / START 자격 조건, 30초 공정, Cycle Re-arm, Critical Safety와 RESET 정책을 정리합니다. 검증 결과는 [Verification Report](verification.md), 결정 과정은 [Design Decisions](design-decisions.md)에 기록합니다.

## READY / START

READY는 Runtime이 관리하는 시작 자격 표시입니다. Core 상태는 Idle을 유지하며, 자격을 충족한 START가 수락되면 Run으로 전환합니다.

| 조건 | 기준 |
|---|---|
| Controller | Idle · Sequence None |
| Cycle | Armed |
| UART | Operational · Communication 정상 |
| Door / E-Stop | fresh Door CLOSED · E-Stop OFF |
| Temperature | valid / fresh · 20~40°C, 양 끝 포함 |
| Pressure | valid / fresh · ADC 1000~2000, 양 끝 포함 |
| 연속 유지 | 모든 조건을 5초 동안 유지 |

Temperature freshness는 5초, Pressure freshness는 1초입니다. 조건 이탈, INVALID, malformed 측정, freshness 만료 또는 통신 장애는 qualification을 취소합니다. 정상 조건으로 돌아오면 새로운 연속 5초가 필요합니다. START 입력 순간에도 현재 조건을 다시 확인합니다.

Humidity는 시작 자격 범위에서 제외하고 Critical Safety에 사용합니다. 시작 자격 범위는 운전 중 Fault 기준과 별도로 관리합니다.

## 30초 공정

시간 기준은 Runtime이 START를 수락한 시점의 `steady_clock`입니다. READY 대기 시간은 공정 시간에 포함하지 않습니다.

| 경과시간 | 이벤트 | SequenceStep | Pump | Heater |
|---|---|---|---|---|
| 0초 | PROCESS START | ProcessStart (6) | OFF | OFF |
| 6초 | PROCESS PUMP ON | PumpOn (2) | ON | OFF |
| 12초 | PROCESS HEATER ON | HeaterOn (4) | ON | ON |
| 18초 | PROCESS HEATER OFF | HeaterOff (10) | ON | OFF |
| 24초 | PROCESS PUMP OFF | PumpOff (11) | OFF | OFF |
| 30초 | PROCESS COMPLETE | ProcessComplete (8) | OFF | OFF |

공정 완료 시 RUN 표시를 끄고 Idle로 전환합니다. `ProcessComplete → Shutdown → None` 후처리를 이후 poll에서 정리하며 출력은 OFF를 유지합니다.

Runtime은 blocking delay 대신 경과시간을 전달합니다. Core는 해당 시각의 단계와 출력을 직접 선택합니다. Poll이 지연되면 이미 지난 단계를 잠깐 켜지 않고 현재 시간 구간에 맞는 출력을 적용합니다. Sensor RX에서는 Interlock과 해당 새 측정의 Critical Safety를 검사하며, Fault가 정상 공정 진행보다 우선합니다.

## Cycle Re-arm

정상 완료 후 Rearm은 WaitOpen입니다. Door가 계속 닫혀 있어도 새로운 READY를 시작하지 않습니다.

| Rearm | 값 | 전환 조건 |
|---|---|---|
| Armed | 0 | READY qualification 가능 |
| WaitOpen | 1 | 정상 공정 완료 후 새 Door OPEN 대기 |
| WaitClose | 2 | OPEN 확인 후 새 Door CLOSED 대기 |

Door OPEN → CLOSED 확인 후 Armed가 되고, 현재 시작 조건을 다시 5초 동안 확인합니다. Error RESET 성공 시에는 Armed / Idle에서 새 qualification을 시작합니다. Re-arm 도중 통신 장애가 발생하면 미완료 Door 이력을 폐기합니다.

## Critical Safety

판정은 새 유효 측정값에 대해 수행합니다. 같은 센서·같은 방향의 Critical 값이 2회 연속 확인되면 해당 Alarm으로 Error에 진입합니다.

| 센서 | Low Critical | High Critical | Alarm |
|---|---|---|---|
| Temperature | ≤5°C | ≥50°C | TemperatureCritical (7) |
| Humidity | ≤15% | ≥90% | HumidityCritical (8) |
| Pressure ADC | ≤200 | ≥3900 | PressureCritical (9) |

Cached 숫자, 다른 센서 line, 반복 poll은 새 측정으로 세지 않습니다. 정상값, INVALID, malformed 값, stale은 해당 streak을 끊습니다. Low에서 High로 바뀌면 새 방향의 첫 측정부터 계산합니다.

운전 중 시작 자격 범위를 벗어난 값만으로 Critical Error가 발생하지 않습니다. 실제 E-Stop, RUN 중 Door OPEN, 통신 상실과 Motor Fault는 기존 Interlock 경로로 처리합니다.

## Error / RESET

Error 진입 시 Pump / Heater / RUN을 OFF, Valve를 CLOSED로 결정하고 RED와 Alarm latch를 유지합니다. Buzzer는 Runtime 송신 기준 약 2초 후 OFF가 되며 Error 해제는 별도 RESET이 필요합니다.

RESET은 다음 조건을 요구합니다.

- Core Error
- fresh Door CLOSED · E-Stop OFF
- Communication 정상 · Motor Fault 없음
- 현재 유효한 Temperature / Humidity / Pressure가 각각 Critical 범위의 엄격한 내부에 있음

RESET 성공 후 Idle / 안전 출력으로 복귀하고 READY 시간을 새로 확인합니다. 정상 측정으로 돌아오거나 통신이 복구된 것만으로 자동 RUN을 재개하지 않습니다.

Operator CLI의 공개 명령은 [CLI Guide](cli.md)에 정리합니다. Runtime stdin의 STOP은 기존 내부 정지 경로를 사용하며, CLI 명령 목록은 `start reset status help exit`입니다.

## 검증 근거

`bf68826`에서 시간 기반 공정과 시작 자격을 구현했습니다. 현재 전체 Host 기준선은 CTest 53/53 PASS입니다. 실제 보드에서는 공정 순서, Pressure READY 이탈 / 복귀, 새 5초 qualification과 정상 범위 Temperature에서 READY 진행을 확인했습니다. Temperature 시작 범위 밖의 HW 시험과 Local RED timing은 [검증 보고서](verification.md)의 후속 항목입니다.

관련 코드: [Core](../modules/core/src/equipment_controller.cpp), [Runtime](../apps/controller/controller_runtime.cpp), [공정 테스트](../tests/transport/test_runtime_cycle.cpp), [READY 테스트](../tests/transport/test_runtime_policy.cpp).
