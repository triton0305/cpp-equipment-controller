# UART Recovery

기준: 2026-10-13 · `c5cbb81`. Linux UART 장애 감지, session 동기화와 안전 재연결을 정리합니다. 구현 계약의 Host 검증과 실제 UART 이상 관찰은 [Verification Report](verification.md), 원인 조사는 [Troubleshooting](troubleshooting.md)에 기록합니다.

## Session 동기화

포트 open은 센서 통신의 유효성을 곧바로 의미하지 않습니다. 성공한 open 뒤 Synchronizing에서 fresh DOOR → 바로 다음 valid ESTOP pair를 기다립니다.

Door 후보는 별도로 보관합니다. 사이에 다른 field, unknown, malformed, 명령 또는 oversized discard event가 오면 후보를 폐기합니다. 새 valid DOOR는 후보를 교체합니다. 정상 pair가 완성되면 두 입력을 함께 Core에 반영하고 Operational / communication recovery로 전환합니다. ESTOP:ON은 기존 안전 평가를 받습니다.

정상 첫 DOOR는 후보로 사용합니다. DHT11 값의 부재나 INVALID는 Door / E-Stop pair의 동기화 조건에 포함하지 않습니다.

Transport의 `readEvents()`는 완성 line과 `OversizedLineDiscarded`를 byte 순서대로 전달합니다. 256자를 초과한 line은 LF까지 폐기하고 line당 한 번 discard event를 만듭니다. `readLines()`는 완성 line을 반환하는 호환 API입니다.

## Deadline과 재연결

| 조건 | 처리 |
|---|---|
| Synchronizing | open 기준 고정 1000 ms 내 정상 pair 요구 |
| Operational | 마지막 정상 Sensor line부터 1000 ms liveness |
| UART open/read/write 실패 | Communication 무효화 · Core Fault · 포트 close |
| Timeout | 안전 출력 송신 시도 · close |
| 포트 close 후 | 같은 경로에 500 ms 간격 재시도 |
| Sensor 대기 중 | 100 ms 간격 안전 complete refresh |
| Recovery | Communication만 복구, 기존 ERROR / Alarm 유지 |

Clock은 `steady_clock`입니다. Noise, incomplete line, unknown, malformed 값, INVALID 측정과 단순 byte 수신은 deadline을 연장하지 않습니다. 한 RX batch는 수신 시각을 공유합니다. 처리나 송신이 지연돼도 그 batch의 남은 line을 새로 받은 데이터처럼 사용하지 않습니다.

Pressure freshness는 1초, Temperature / Humidity는 각각 5초입니다. 전체 stream liveness와 개별 측정 validity를 구분합니다.

## 안전 출력과 MCU 동기화

초기 / 재연결의 첫 송신은 이전 MCU partial command를 마치는 LF와 다음 6-command 묶음입니다.

`PUMP → HEATER → VALVE → BUZZER → RUN_LED → ERROR_LED`

Pump / Heater / RUN OFF, Valve CLOSED를 강제하고 Error / Buzzer는 현재 정책을 따릅니다. 기존 ON 출력의 재전송 queue는 유지하지 않습니다. 실패한 RX batch의 나머지도 재사용하지 않습니다.

STM32는 완전한 safe refresh로 latch를 해제한 뒤 별도 정상 refresh에서 ON을 허용합니다. Linux write 성공과 MCU의 실제 latch 해제 확인은 구분합니다. ACK / session timestamp가 없어 원격 생성 시각을 식별할 수 없는 입력은 실물 계측 대상입니다.

통신이 끊겨 Linux OFF 명령을 전달할 수 없을 때는 [STM32 Runtime](../firmware/stm32-equipment-io/stm32-runtime.md)의 local fail-safe가 출력을 차단합니다.

## Fault와 명령

Core는 기존 Error 상태의 Alarm을 보존합니다. 재연결로 자동 RESET / START를 수행하지 않습니다. 명시적 RESET은 [Process Control](process-control.md)의 안전 조건을 요구하고, RESET 후 새 READY qualification이 필요합니다.

UART 오류에서도 Runtime 프로세스는 유지됩니다. 장치가 다른 이름으로 재등장한 경우에는 실행 경로를 맞춰야 합니다. 사용법은 [CLI Guide](cli.md)를 참조합니다.

## 검증과 현재 조사 상태

Host regression은 fragment, pair 순서, 후보 중단, oversized event ordering, split read, timeout 경계, Error latch와 READY / Re-arm을 검증합니다. 당시 synchronization 수정 전체 CTest는 45/45 PASS였고 현재 전체 기준선은 53/53 PASS입니다.

실제 장비에서는 일부 malformed line과 간헐적인 약 1초 telemetry 공백을 관찰했습니다. 조사한 공백 구간에서도 Linux poll은 약 10 ms 주기로 이어졌습니다. 발생 계층은 미확정이며 STM32 TX와 Linux RX의 동시 계측을 후속 과제로 관리합니다.

1000 ms는 소프트웨어 판정 기준입니다. 물리 출력까지의 정확한 응답시간과 장시간 UART 안정성은 별도 HW 계측 범위입니다.
