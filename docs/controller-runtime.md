# Controller Runtime

Linux Runtime은 UART lifecycle, 센서 freshness, 운전 명령, READY 자격, 공정 시간과 출력 송신을 관리합니다. Core가 State / Sequence / Alarm을 결정하고 STM32가 입력 수집과 GPIO 출력 적용을 담당합니다.

기준: 2026-10-13 · `c5cbb81`. 전체 검증은 [Verification Report](verification.md)를 참조합니다.

## 실행과 책임

Terminal 1에서 `./run`으로 Runtime을 실행하고 Terminal 2에서 `./ctl`로 Operator Console을 연결합니다. Runtime은 SIGINT / SIGTERM까지 지속 실행합니다.

| 구성 | 책임 |
|---|---|
| `EquipmentController` | State · Sequence · Alarm · Interlock · Critical 판정 |
| `ControllerRuntime` | UART session · freshness · qualification · 시간 · 출력 송신 |
| `SerialTransport` | termios · RX framing · read/write · disconnect |
| `control_ipc::Server` | Unix socket 요청을 Runtime dispatcher에 전달 |
| `equipment_ctl` | 명령 입력 · 응답 표시 |
| STM32 Runtime | Sensor telemetry · GPIO · 독립 command-loss fail-safe |

Main loop는 Runtime poll, 기존 stdin 입력, IPC 요청을 처리하고 약 10 ms sleep합니다. IPC server는 non-blocking I/O로 한 회에 최대 한 요청을 처리합니다. Controller 변경은 같은 Runtime 흐름에서 수행합니다.

## 명령과 Snapshot

Stdin / UART 명령과 IPC 제어 요청은 기존 Runtime guard를 사용합니다. START / RESET 조건과 공정은 [Process Control](process-control.md)에 정리합니다.

CLI에서는 `start reset status help exit`를 제공합니다. `help`와 `exit`은 CLI 로컬에서 처리합니다. `status`는 Runtime이 보유한 snapshot을 반환하며 새 UART 요청을 보내지 않습니다. CLI 연결 종료는 Runtime과 현재 공정을 유지합니다.

Snapshot의 Pump / Heater는 Controller가 결정한 출력 상태입니다. 자세한 형식과 응답 의미는 [CLI Guide](cli.md)에 기록합니다.

## 센서 유효성과 Freshness

| 항목 | 기준 |
|---|---|
| 전체 유효 Sensor stream | 마지막 정상 line부터 1000 ms |
| Pressure | 마지막 유효 측정부터 1000 ms |
| Temperature / Humidity | 각 마지막 유효 측정부터 5000 ms |
| STATUS | 약 1초마다 현재 snapshot |
| Buzzer | Error 경보 송신 시작부터 약 2초 |

INVALID / malformed 측정은 해당 validity를 해제합니다. 정상 측정값을 수신하기 전이나 만료된 값은 `unavailable`로 표시합니다. 다른 센서 수신은 개별 온습도 freshness를 연장하지 않습니다.

## UART Session과 장애

성공한 open 후 Synchronizing에서 안전 출력 refresh를 유지하고, 인접한 fresh DOOR → valid ESTOP pair로 Operational에 진입합니다. 동기화 중 START / RESET을 거부하고 READY 시간을 취소합니다.

Open / read / write 오류 또는 sensor timeout은 CommunicationFault를 반영하고 포트를 닫습니다. 같은 장치 경로에 500 ms 간격으로 재연결을 시도하며 기존 Error / Alarm은 유지합니다. 상세 계약은 [UART Recovery](uart-recovery.md)를 참조합니다.

## 출력 갱신

기존 6-command complete refresh 뒤에 STATE 명령을 붙여 STM32에 전송합니다.

일반 Sensor-driven 갱신은 한 RX batch의 상태를 반영한 뒤 모아서 보냅니다. 새로운 정상 batch는 출력이 같아도 command liveness를 refresh합니다. Fault 진입은 다음 RX event 처리 전에 안전 출력을 송신합니다. START / RESET 등 실제 상태 변경의 송신 경로도 유지합니다.

수신 timestamp는 batch가 들어온 시각을 유지하며 송신 지연 뒤의 처리 시각으로 freshness를 연장하지 않습니다. `96cac56`의 동작과 검증 근거는 [Troubleshooting](troubleshooting.md)에 기록합니다.

## B1 RESET과 종료

STM32는 기존 PC13 입력을 foreground에서 읽고 30 ms debounce와 press당 한 번의 RESET 요청을 사용합니다. 최신 입력 snapshot 뒤의 RESET은 Linux의 기존 guard로 다시 평가합니다.

SIGINT / SIGTERM에서는 Core STOP과 안전 출력 송신을 시도한 뒤 UART를 닫습니다. 프로세스 crash나 송신 불가 상황의 물리 출력 차단은 STM32 local fail-safe가 담당합니다.

## 진단과 검증

`EQUIPMENT_UART_DIAG`로 [UART Diagnostics](uart-diagnostics.md)를 활성화합니다. Runtime 제어 중 고정 메모리에 계측을 보관하고 종료 후 CSV로 저장합니다.

현재 전체 CTest는 53/53 PASS입니다. 실제 CLI 연결·상태 조회·명령과 공정 검증, 남아 있는 UART 관찰과 추가 HW 범위는 [검증 보고서](verification.md)에서 구분합니다.
