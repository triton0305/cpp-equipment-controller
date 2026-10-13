# Operator CLI

`./run`은 Controller Runtime과 STM32 UART를 소유하고 Safety / Sequence 판단,
센서 polling, 상태 로그 출력을 수행합니다. 별도 터미널의 `./ctl`은 로컬 Unix
Domain Socket으로 명령만 전달하며 UART 장치를 열지 않습니다.
기존 `./run`의 stdin START / STOP / RESET 입력도 계속 사용할 수 있습니다.

## 빌드 및 실행

프로젝트 루트에서 빌드합니다. Host 테스트에는 Python 3도 필요합니다.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

Terminal 1:

```bash
./run                     # 기본 /dev/ttyACM0
# 또는 ./run /dev/ttyACM1
```

Terminal 2:

```text
$ ./ctl
Equipment Controller CLI
Connected.
ctl> status
ctl> start
ctl> reset
ctl> help
ctl> exit
```

## 명령

명령은 한 줄에 하나씩 입력합니다. 대소문자를 구분하지 않으며 LF / CRLF를
지원합니다. 인수와 앞뒤 공백은 지원하지 않습니다.

| 명령 | 동작 |
|---|---|
| `start` | 기존 Runtime START → Core 경로로 전달합니다. |
| `reset` | 기존 RESET 경로로 Fault recovery를 시도합니다. |
| `status` | Runtime이 보유한 최신 상태 snapshot을 한 줄로 반환합니다. |
| `help` | CLI에서 지원 명령 목록을 출력합니다. |
| `exit` | CLI만 종료합니다. Runtime과 Sequence, 출력 상태에는 영향을 주지 않습니다. |

지원 명령은 `start reset status help exit`입니다.
지원하지 않는 명령(`stop` 포함)은 CLI에서 `ERROR: invalid command; use help`로
표시하고 IPC로 전달하지 않습니다. 기존 stdin 및 IPC server의 STOP 처리는 유지됩니다.
`help`와 `exit`은 CLI 내부에서 처리하므로 Runtime에 전달되지 않습니다.
Ctrl+D / CLI 연결 종료도 STOP이나 Fault를 발생시키지 않습니다.

### 제어 명령의 결과와 거부 조건

기존 `ControllerRuntime::handleCommand()`가 성공하면 `OK`를 반환합니다.
실패하면 다음 응답을 반환합니다.

```text
REJECTED: conditions not met, no state change, or output failure; use status
```

이 응답은 상세 원인 진단이나 명령 rollback을 의미하지 않습니다. 기존 함수는
조건 거부뿐 아니라 상태 변화 없음과 UART 출력 실패도 false로 반환합니다.
출력 실패 시 상태가 이미 바뀌었을 수 있으므로 `status`와 Runtime 로그를 확인합니다.

- 모든 제어 명령은 기존 UART 연결, Operational 및 freshness 검사를 거칩니다.
- START는 기존 5초 READY qualification을 요구합니다. Idle / Sequence None,
  Armed, Door CLOSED, E-Stop OFF, 유효하고 범위 내인 Temperature / Pressure,
  정상 통신 조건 및 Core Safety 판단을 그대로 사용합니다.
- RESET은 Error 상태 및 최신 Door / E-Stop 정보가 필요하며, 기존 Core가
  Door, E-Stop, Motor, 통신, 센서 유효성과 Critical 정상 범위를 검사합니다.
  Fault 원인이 남아 있으면 거부됩니다.
- Alarm, Rearm, Sequence 관련 기존 정책을 우회하는 경로는 없습니다.

### 상태 snapshot

예시 형식:

```text
State=Idle Ready=1 Armed=1 Rearm=0 Alarm=0 SequenceStep=0 Operational=1 CommunicationOK=1 Door=CLOSED EStop=OFF Pressure=1500 Temperature=25.0 Humidity=45.0 Pump=OFF Heater=OFF
```

`State`는 Idle / Ready / Run / Error이며 `Ready`는 별도의 READY qualification
indicator입니다. `Armed`는 재시작 허용 상태입니다. `Rearm`은 0=Armed,
1=WaitOpen, 2=WaitClose이고, `SequenceStep`과 `Alarm`은 기존 enum 숫자입니다.
`Operational`은 Runtime이 보유한 동기화 phase, `CommunicationOK`는 보유 통신 유효성입니다. 현재 통신 상태는 두 필드를 함께 확인합니다.
Pressure는 ADC raw count, Temperature는 °C, Humidity는 %입니다.
유효하지 않거나 기존 polling에서 만료된 측정은 `unavailable`, 아직 받지 못한
Door / EStop은 `unknown`으로 표시합니다. Pump / Heater는 Controller의 출력 명령
상태이며 실제 장치 피드백이 아닙니다.

`status` 자체는 UART 요청, 새로운 센서 polling, 상태 변경을 수행하지 않습니다.
Runtime의 정기 polling으로 갱신된 snapshot을 읽습니다.

## 연결 및 lifecycle

기본 endpoint는 `/tmp/equipment-controller-<uid>/control.sock`입니다.
사용자 전용 디렉터리(0700)와 소켓(0600)을 사용합니다. 별도 endpoint가 필요하면
두 터미널에서 동일한 `EQUIPMENT_CONTROL_SOCKET` 경로를 지정합니다. 해당 경로의
부모 디렉터리는 미리 준비해야 합니다.

Runtime이 없거나 endpoint에 연결할 수 없으면 CLI는 다음 오류와 함께 종료합니다.

```text
Controller runtime is not running or IPC endpoint is unavailable.
```

Runtime 종료 시 입력 대기 중인 CLI도 `Controller runtime connection closed.`를
출력하고 오류 종료합니다. 자동 reconnect는 없습니다. CLI 하나를 지원하며 추가
연결은 닫습니다. CLI를 다시 실행해 연결할 수 있습니다.

Runtime은 endpoint 잠금을 얻은 뒤 기존 소켓의 연결 거부를 확인한 경우에만
stale socket을 제거합니다. 활성 endpoint나 일반 파일을 삭제하지 않습니다.
정상 종료 시 소켓을 제거하며, `.lock` 파일은 잠금 inode의 일관성을 위해 남깁니다.
IPC 초기화 실패 시 Runtime은 UART를 열기 전에 오류 종료합니다.

서버는 Runtime main loop에서 non-blocking 방식으로 한 회당 최대 하나의 요청을
처리합니다. 부분 수신 / 송신을 지원하고 입력 길이는 256 byte로 제한합니다.
지나치게 긴 IPC 요청은 연결을 닫으며, 느린 client 때문에 UART loop를 기다리지
않습니다. 별도 receiver thread가 Controller 상태를 변경하지 않습니다.

## 검증 범위

**HOST VERIFIED**: 명령 parsing / 전달 / 거부, CLI `stop`의 로컬 거부 및 IPC 미전달,
CLI `stop` 입력 후 Run 상태 유지, snapshot의 UART 무접근,
stdin과 IPC의 공통 처리, disconnect, Runtime 종료, stale socket,
활성 endpoint 보호, 부분 수신 및 느린 client, 기존 Runtime / UART 회귀 테스트.
Host integration은 Unix socket과 PTY로 STM32 입력을 모사합니다.

**HW VERIFIED — 사용자 실물 확인**: 실제 NUCLEO-F411RE와 Runtime을 연결한 환경에서 CLI 연결, HELP, STATUS, READY 미충족 START 거부, 정상 START, 30초 공정 진행, RESET과 STOP 로컬 거부를 확인했습니다.

실물 STATUS는 Runtime이 보유한 센서와 출력 상태를 조회한 결과입니다. 전체 IPC 예외 처리, CLI 종료 후 lifecycle 및 느린 client 시험은 위 Host 결과로 구분합니다.

최신 전체 자동 테스트는 **CTest 53/53 PASS**입니다. 상세 결과와 확인 범위는 [Verification Report](verification.md)를 참조합니다. 공정과 명령 조건은 [Process Control](process-control.md), 실제 장애 조사는 [Troubleshooting](troubleshooting.md)에 연결합니다.
