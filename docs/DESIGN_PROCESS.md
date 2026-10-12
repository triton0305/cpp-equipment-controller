# Equipment Controller 설계 의사 결정과 AI 협업 기록

사용자 · ChatGPT · Codex | 작성일 2026.10.13 KST

### 검토 자료

이 문서는 Equipment Controller 프로젝트의 개발·회의 대화에서 확인된 역할과 의사 결정 과정을 바탕으로 작성했다. 특히 개발 (13) 전후의 READY qualification, RUN sequence, Door re-arm, Critical Safety, RESET, DHT11·Pressure 상태 확인, UART 복구, STATUS 출력 정책과 실제 하드웨어 검증 과정을 다룬다.

이 문서의 목적은 기능 목록을 다시 나열하는 것이 아니라, **누가 문제를 발견했고, 누가 정책을 결정했으며, 누가 기술적으로 구체화했고, 누가 실제 코드를 수정하고 검증했는지**를 분리해 기록하는 데 있다.

실제 소스 변경은 상당 부분 Codex가 수행했기 때문에 사용자와 ChatGPT만으로 역할을 나누지 않는다. 사용자, ChatGPT, Codex를 별도 주체로 두고 기록한다.

## 1 역할과 기여의 핵심

사용자는 장비가 실제로 어떻게 동작해야 하는지 판단하고 최종 정책을 결정했다. Door가 닫힌 뒤 일정 시간 안전 조건을 확인해야 한다는 READY 정책, 한 번의 RUN을 30초로 제한하는 운전 정책, RUN 종료 후 Door를 다시 열고 닫아야 다음 사이클을 준비할 수 있는 re-arm 정책, E-Stop과 RESET의 운용 순서, 센서 Critical Safety 범위와 표시 장치의 의미를 직접 정했다. 또한 STM32 보드를 실제로 조작하면서 Door·E-Stop·B1·가변저항·DHT11·UART를 확인하고, 프로그램 실행 로그와 실제 장비 반응을 기준으로 구현 결과를 검증했다.

ChatGPT는 사용자의 운용 의도를 상태 머신, 타이머, 인터록, sequence, 통신 복구 조건으로 구조화했다. 실제 로그를 바탕으로 원인 후보를 계층별로 분리하고, Codex가 구현할 수 있는 수정 명세와 회귀 테스트 조건을 작성했다. Host Test와 Hardware Test를 구분하고, 구현된 코드가 있다고 해서 곧바로 HW VERIFIED로 기록하지 않도록 검증 기준도 관리했다.

Codex는 확정된 요구와 명세를 바탕으로 C++ Controller와 STM32 firmware의 실제 소스를 수정하고 Build·CTest·Host regression test를 수행했다. READY timer, sequence progression, RUN timer, B1 RESET polling, STATUS 출력, DHT11 production path, critical sensor safety, Door re-arm, command parsing과 관련 테스트가 Codex 작업으로 보고됐다.

| 주제 | 사용자의 문제 인식과 판단 | 사용자의 제안 또는 요구 | ChatGPT의 분석과 구체화 | Codex의 구현 역할 |
| --- | --- | --- | --- | --- |
| 상태 표시 | 장비 상태와 Pump/Heater 실제 동작 표시를 구분해야 함 | RGB는 상태, 별도 초록/노랑 LED는 Pump/Heater 작동 표시 | 상태 LED와 actuator indicator 역할 분리 | 출력 로직 반영 |
| READY | Door가 닫혔다고 바로 운전 준비로 보면 안 됨 | 5초 연속 안전 확인 후 READY | qualification 조건·reset 조건 구조화 | timer와 gate 구현·테스트 |
| RUN | START 이후 공정이 일정 시간 안에 종료돼야 함 | 한 시퀀스 30초 | RUN timer와 sequence 종료 조건 구분 | 30초 timer 구현 |
| Sequence | START 직후 Pump·Heater가 한꺼번에 켜지는 동작이 부자연스러움 | 단계적으로 진행 | sensor RX와 state advance 분리, one-poll-one-step 명세 | sequence progression 수정 |
| Door re-arm | RUN 종료 후 바로 다시 시작하면 안 됨 | Door OPEN 1회 후 다시 CLOSE·5초 확인 | re-arm 상태 흐름 정의 | re-arm 상태와 회귀 테스트 구현 |
| Critical Safety | 센서가 명백히 비정상일 때 자동 정지가 필요 | 온·습도·압력 critical threshold 결정 | safety threshold와 progression threshold 분리 | critical fault 처리 구현 |
| E-Stop / RESET | 물리 해제만으로 ERROR가 풀리면 안 됨 | 안전 확인 후 B1 RESET | ERROR latch·reset guard 구조화 | reset input/guard 구현 |
| DHT11 / Pressure | 실제 값이 STATUS와 UART에 보여야 함 | DHT11·압력값 상태 표시 및 safety 활용 | Linux–UART–STM32 계층 진단 | production path와 SensorState 연결 |
| UART 복구 | 재연결 시 stale/fragment 문제가 실제로 발생 | 정상 복구와 safe state 유지 | stream fragment, stale watchdog, recovery timing 분석 | 일부 복구 로직 구현, race는 후속 조사 |
| STATUS | 상태가 너무 많이 출력됨 | 주기 상태 출력은 1초마다 | control loop와 log cadence 분리 | 1초 STATUS 출력 구현 |
| Git 기준선 | 검증된 변경만 기준선으로 남겨야 함 | commit/push는 사용자 직접 수행 | commit 시점·메시지·검증 상태 정리 | commit/push 수행하지 않음 |

### 판단 근거의 표현 기준

사용자가 직접 말한 요구, 실제 장비에서 관찰한 결과, 실행한 명령과 로그는 사용자 기여로 기록한다. ChatGPT가 제안한 값이나 원인 후보는 사용자 결정 또는 실제 검증과 구분한다. Codex가 수정했다고 보고된 기능은 구현 역할로 기록하되, 실제 하드웨어에서 사용자가 확인하기 전에는 HW VERIFIED로 자동 승격하지 않는다.

대화 중 조사했지만 실제 원인이 아니었던 가설도 최종 원인과 분리한다. 예를 들어 `main.cpp`가 `ControllerRuntime::handleCommand()`를 우회할 가능성은 조사 과정의 가설이었지만 실제 코드에서는 우회하지 않는 것으로 확인됐다. 또한 READY 5초 기능이 없다고 성급히 판단한 시점도 있었으나 이후 코드에서 `ReadyDelay = 5s`가 확인됐으므로, 최종 문서에서는 “미구현”이 아니라 실행 경로·바이너리 기준선 확인 문제로 구분한다.

## 2 장비 상태 표시와 실제 동작 표시의 분리

### 사용자의 역할 정의

사용자는 장비에 RGB 상태 LED와 별도의 초록 LED, 노란 LED가 있다는 점을 다시 짚었다. 이어 별도 초록 LED와 노란 LED는 각각 Pump와 Heater의 **실제 작동 표시**이고, DHT11과 가변저항으로 입력하는 압력값은 **장비 상태 확인용 센서 값**이라고 정리했다.

> 발언 요지: “Pump와 Heater는 작동 표시, 압력값(가변저항)과 DHT11은 상태 표시.”

이 판단은 단순히 LED 색을 정하는 일이 아니라, **장비 전체 상태와 actuator 실제 동작을 서로 다른 표시 체계로 분리한 것**이다.

RGB LED는 IDLE·READY·RUN·ERROR처럼 Controller의 상태를 나타내고, 별도 초록/노란 LED는 Pump와 Heater의 출력 상태를 나타낸다. 따라서 RGB가 RUN을 나타내는 초록색이어도 별도 Pump LED가 OFF일 수 있고, sequence 특정 단계에서 Pump가 ON일 때만 별도 초록 LED가 켜진다.

### ChatGPT의 구체화

ChatGPT는 사용자의 구분을 다음 책임으로 정리했다.

| 표시 또는 입력 | 의미 |
| --- | --- |
| RGB LED | Controller 전체 상태 |
| 별도 초록 LED | Pump 실제 ON/OFF |
| 별도 노란 LED | Heater 실제 ON/OFF |
| DHT11 | Temperature/Humidity 상태 |
| 가변저항 | Pressure 입력 모사 및 상태 |
| Door | 운전 준비·인터록 조건 |
| E-Stop | 최우선 비상정지 입력 |

상태 표시와 actuator 표시를 분리함으로써 READY 상태에서도 Pump와 Heater는 OFF일 수 있고, RUN 상태 안에서도 sequence step에 따라 두 출력이 서로 다른 시점에 켜질 수 있도록 해석했다.

### 역할과 설계 결과

사용자는 실제 장비에서 어떤 표시가 무엇을 뜻해야 하는지 정했다. ChatGPT는 이 요구를 Controller state와 physical output의 책임 분리로 구체화했다. Codex는 확정된 출력 정책을 소스에 반영하는 구현 역할을 맡았다.

## 3 Door CLOSED 이후 5초 READY qualification

### 사용자의 문제 확인과 요구

사용자는 Door가 닫혔다고 즉시 운전 준비 상태로 보는 대신, 일정 시간 안전 상태가 유지되어야 한다고 요구했다. 초기 논의에서 2초도 검토됐지만 최종적으로 5초를 선택했다.

> 발언 요지: “5초 조건 왜 구현 안 됐어?”

이 질문은 단순히 timer 존재 여부를 묻는 것이 아니라, 실제 `./run` 동작에서 사용자가 기대한 READY qualification이 보이는지를 확인한 것이다.

READY qualification의 핵심 조건은 다음과 같이 정리됐다.

```text
Door CLOSED
+ E-Stop OFF
+ UART communication valid
+ Controller not in ERROR
-------------------------------
5초 연속 유지
→ READY
```

중간에 Door OPEN, E-Stop ON, communication loss, ERROR가 발생하면 qualification timer를 초기화한다.

### ChatGPT의 분석과 수정

ChatGPT는 처음에 실제 실행에서 5초 조건이 보이지 않는다는 이유로 구현이 없는 것처럼 성급히 반응한 적이 있다. 이후 코드와 실행 경로를 확인하면서 `ReadyDelay = 5s`가 존재하는 것을 확인했고, 구현 부재와 실행 바이너리 문제를 구분해야 했다.

이 과정에서 최종적으로 중요한 검증 기준은 다음처럼 정리됐다.

- 소스에 5초 상수가 존재하는가
- `START`가 qualification 이전에 차단되는가
- Door OPEN 또는 ERROR 발생 시 timer가 초기화되는가
- 실제 `./run`이 최신 build 바이너리를 실행하는가
- HW에서 실제 약 5초 이후 READY가 되는가

### Codex의 역할

Codex는 READY timer와 command gate를 코드 및 테스트에 반영하는 역할을 맡았다. 대화 후반에는 READY qualification 관련 regression test와 runtime 정책 정리가 보고됐다.

### 역할과 논의 결과

사용자는 실제 장비 행동을 기준으로 READY qualification의 존재 여부를 문제 삼고 5초라는 운영 정책을 확정했다. ChatGPT는 조건과 reset rule을 명세화하고 실행 경로까지 검증해야 한다는 기준을 정리했다. Codex는 실제 구현과 Host Test를 담당했다.

## 4 START sequence와 Pump·Heater 단계 진행

### 사용자가 실물 동작에서 문제를 발견한 과정

READY가 표시된 이후 사용자는 Pump와 Heater가 어떻게 동작하는지 확인했다.

> 발언 요지: “READY는 1로 표시가 되는데 Pump랑 Heater는 틀 생각 없어?”

START 실행 뒤 제공된 로그에서는 sequence가 매우 빠르게 마지막 단계까지 진행하고 Pump와 Heater가 함께 ON 상태가 되는 모습이 나타났다.

```text
START
EVENT Controller state=2 alarm=0
...
SequenceStep=7
Pump=1
Heater=1
```

사용자는 다시 다음처럼 지적했다.

> 발언 요지: “근데 보통 한 번에 전부 다 켜지는 게 맞아?”

이 질문으로 단순히 출력이 켜졌다는 사실보다 **실제 공정이 단계적으로 진행되어야 한다는 운용 관점**이 드러났다.

### ChatGPT의 원인 구조화

ChatGPT는 START 이후 sensor line이 들어올 때마다 sequence가 과도하게 advance되는 구조를 의심했고, sensor update와 sequence transition을 분리해야 한다고 정리했다.

명세는 다음과 같은 형태로 구조화됐다.

```text
START
→ CheckDoor
→ PumpOn
→ CheckPressure
→ HeaterOn
→ CheckTemperature
→ ProcessStart
→ RUN
```

핵심은 한 번의 poll 또는 하나의 조건 갱신에서 여러 step을 한꺼번에 통과하지 않도록 하는 것이다. 이를 one-poll-one-step 형태로 설명했다.

### Codex의 구현

Codex는 sequence progression을 수정하고 sensor RX와 sequence transition을 분리하는 작업, 관련 regression test를 수행한 것으로 보고됐다.

### 역할과 논의 결과

문제의 출발점은 사용자의 실제 장비 관찰이었다. ChatGPT는 로그에서 sequence progression 문제를 구조화했고, Codex가 실제 코드를 수정했다. 이후 최종 합격 여부는 다시 실제 HW에서 단계 진행을 확인해야 하는 항목으로 남는다.

## 5 RUN 30초와 사이클 종료

### 사용자가 정한 운전 시간

사용자는 한 번의 sequence를 30초로 하도록 개발 측에 전달했다고 명확히 말했다.

> 발언 요지: “한 시퀀스는 30초로 하자고 개발한테 말했다.”

따라서 30초는 READY qualification의 5초와 다른 시간이다.

```text
Door CLOSED
→ 5초 qualification
→ READY
→ START
→ RUN 시작
→ 30초 운전
→ RUN 종료
```

### ChatGPT의 구체화

ChatGPT는 30초를 단순 LED 점등 시간이 아니라 **START 이후 실제 RUN 상태의 최대 운전 시간**으로 구분했다. Pump와 Heater는 30초 동안 항상 ON인 것이 아니라 sequence step에 따라 개별 제어될 수 있다.

또한 RUN timer가 sequence step timer와 별개인지, 30초가 도달했을 때 현재 step과 무관하게 출력이 안전하게 종료되는지 확인해야 한다고 정리했다.

### Codex의 역할

RUN 시작 시점을 기준으로 30초 timer를 적용하고, 종료 시 state와 output을 정리하는 구현 및 테스트가 Codex 작업으로 보고됐다.

## 6 RUN 종료 후 Door re-arm

### 사용자의 운용 판단

사용자는 RUN이 끝난 뒤 장비가 즉시 다시 READY가 되어 재시작하는 흐름을 원하지 않았다.

> 발언 요지: “그러고 다시 바로 시작하는 건 아니지? 문 다시 열었다가 닫아야지?”

이어 Door를 실제로 다시 열었다가 닫는 과정이 장비 운용상 자연스러운지도 검토했다.

> 발언 요지: “Door OPEN 하는 게 더 자연스러운 거야?”

이 논의를 통해 다음 흐름이 정리됐다.

```text
RUN 종료
→ IDLE / re-arm 대기
→ Door OPEN 1회 확인
→ Door CLOSED
→ 5초 qualification
→ READY
```

### 사용자 기여의 의미

이 정책은 단순 state transition이 아니라 **새 공정 사이클이 시작됐다는 물리적 행위를 Door 동작으로 확인**하려는 요구다. RUN이 끝났다고 자동 재가동 조건이 생기지 않도록 하고, 사용자가 장비를 다시 열고 닫은 뒤 준비 과정을 새로 시작하도록 했다.

### ChatGPT의 구체화

ChatGPT는 이를 WaitOpen·WaitClose 또는 동등한 re-arm 상태로 구조화하고, 이전 cycle의 READY timer나 sequence state가 남지 않아야 한다고 정리했다.

### Codex의 구현

Codex는 Door re-arm state와 관련 Host regression test를 구현한 것으로 보고됐다.

## 7 Critical Safety와 공정 진행 조건의 분리

### 사용자가 제안한 자동 보호

사용자는 원래 사람이 Temperature·Humidity·Pressure를 보고 비정상 상황에서 E-Stop을 누르는 흐름을 확인한 뒤, 명백한 이상값에서는 Controller가 자동으로 ERROR로 전환하는 기능을 추가로 요구했다.

> 발언 요지: “임계치를 넘어가면 E-STOP으로 넘어가게 설계까지. 임계치는 진짜 비정상적이어야 함.”

Temperature 상한은 대안 검토 뒤 사용자가 직접 다음처럼 수정했다.

> 발언 요지: “50도 이상으로 하자.”

대화에서 정리된 Critical Safety 범위는 다음과 같다.

| Sensor | Critical condition |
| --- | --- |
| Temperature | `<= 5°C` 또는 `>= 50°C` |
| Humidity | `<= 15%` 또는 `>= 90%` |
| Pressure ADC | `<= 200` 또는 `>= 3900` |

### Safety와 restart/progression의 구분

사용자는 critical threshold가 “다시 시작해도 되는 조건”과 같은 의미가 아닌지 확인했다.

> 발언 요지: “공정 중 저게 비정상이니까 정지시키는 조건인 거지, 다시 시작해도 되는 조건은 아니라는 말이잖아?”

이 질문으로 다음 세 개념을 분리했다.

1. **Critical Safety Threshold**  
   장비를 즉시 ERROR로 보내야 하는 극단적인 비정상 범위.

2. **RESET Safety Guard**  
   ERROR 원인이 해소됐는지 판단하고 RESET을 허용할 조건.

3. **Process Progression Threshold**  
   `CheckPressure`, `CheckTemperature` 등에서 다음 공정 단계로 넘어가기 위한 목표값.

공정 진행용 정상 조건은 당시 확정하지 않고 후속 작업으로 남겼다. 따라서 중간에 제안된 Pressure 1500~2500, Temperature 15~35, Pressure >=2000 같은 숫자는 최종 정책으로 기록하지 않는다.

### ChatGPT의 역할

ChatGPT는 사용자의 “비정상적이어야 한다”는 요구를 safety threshold로 구조화하고, progression target과 섞이지 않도록 개념을 분리했다. 또한 critical fault가 발생하면 기존 E-Stop과 동일한 ERROR/safe-output 체계로 들어갈 수 있도록 설계 방향을 정리했다.

### Codex의 역할

AlarmCode와 critical sensor fault 처리, SensorState 연결 및 회귀 테스트가 Codex 작업으로 보고됐다.

## 8 E-Stop, ERROR latch와 B1 RESET

### 사용자가 확정한 복구 철학

사용자는 E-Stop 물리 버튼을 풀었다고 해서 장비가 자동 복구되는지 확인했다.

> 발언 요지: “안전 확인되고 RESET을 눌러야 E-Stop 해제인 거지?”

이 질문은 ERROR 상태의 해제를 **원인 해소 + 명시적 RESET**으로 분리한 것이다.

최종 운용 흐름은 다음과 같이 정리됐다.

```text
E-Stop 발생
→ ERROR
→ Pump/Heater 등 안전 출력 OFF
→ ERROR 표시
→ Buzzer 경보
→ E-Stop 물리 해제
→ Door/Sensor/Communication 안전 확인
→ B1 RESET
→ ERROR 해제
→ IDLE
→ qualification 재시작
```

### Buzzer 2초 정책

대화에서 ERROR 발생 시 Buzzer는 약 2초 동안 동작하고 이후 OFF되며, ERROR LED와 ERROR latch는 계속 유지하는 정책이 정리됐다.

즉 다음은 서로 다른 사건이다.

```text
Buzzer OFF
≠
ERROR 해제
```

### ChatGPT의 구체화

ChatGPT는 RESET 직후 이전 RUN timer, sequence state, READY qualification이 남아 있지 않아야 한다고 정리했다. 또한 E-Stop과 communication fault가 겹치는 경우 alarm priority와 safe output이 일관되게 유지되는지 검토 대상으로 두었다.

### Codex의 구현

B1 polling, RESET 요청 처리, 기존 `handleReset()` guard와 연계된 변경이 Codex 작업으로 보고됐다.

## 9 DHT11과 Pressure 상태 확인 및 실제 HW 검증

### 사용자가 실제 문제를 발견한 과정

사용자는 Linux STATUS에서 다음과 같은 출력을 보고 문제를 제기했다.

```text
Temperature=unavailable
Humidity=unavailable
```

> 발언 요지: “왜 unavailable임?”

이 문제는 코드 리뷰만으로 끝나지 않고 실제 UART raw data를 확인하는 방향으로 진행됐다.

사용자는 실제 환경에서 다음과 같은 명령을 실행해 UART를 확인했다.

```bash
stty -F /dev/ttyACM0 115200
cat /dev/ttyACM0
```

초기에는 Door·E-Stop·Pressure만 보이고 TEMP/HUMIDITY가 없는 상황이 있었고, WSL USB 연결과 STM32 firmware 기준선을 확인한 뒤 production firmware를 다시 Flash했다.

이후 사용자가 실제로 확인해 전달한 값에는 다음이 포함됐다.

```text
TEMP:21.3
HUMIDITY:46.2
```

Linux runtime에서도 Temperature·Humidity 값이 정상적으로 나타나는 것을 확인했다.

### 역할 구분

```text
사용자
= UART 명령 실행
+ USB 재연결
+ STM32 Flash
+ 실제 센서값 확인

ChatGPT
= Linux STATUS → raw UART → WSL USB → firmware
  순서로 진단 계층 분리
+ 결과 해석
+ 다음 확인 절차 제시

Codex
= DHT11 production path
+ Humidity SensorState 연결
+ 관련 Host Test
```

이 구조 때문에 DHT11은 단순 Host PASS와 실제 production path HW 확인을 구분해 기록할 수 있었다.

### Pressure 입력

Pressure는 가변저항을 사용해 ADC 값을 변화시키고, 사용자가 직접 값을 조절하면서 상태와 Critical Safety를 확인하는 입력으로 사용됐다. Pressure 값은 장비 상태 표시와 safety 판단에 사용하되, 공정 progression 목표값은 별도 정책으로 남겼다.

## 10 UART Recovery와 실제 reconnect edge case

### 기존 안전 구조

Linux Controller와 STM32 사이에는 UART 통신 장애에 대비한 다음 정책이 존재했다.

- Linux Sensor Timeout
- UART reconnect
- Safe output refresh
- STM32 Command-loss Fail-safe
- ERROR/RESET 기반 재가동 제한

ChatGPT는 새 READY/RUN 정책이 기존 통신 안전 기능을 약화시키지 않도록, UART가 끊겼을 때 READY qualification을 취소하고 Pump/Heater 등 출력을 안전 상태로 유지하는 방향을 검토했다.

### 사용자가 실제로 발견한 reconnect 문제

후반 재실행 과정에서 사용자가 다음과 같은 로그를 제공했다.

```text
Invalid SENSOR line: 6
EVENT sensor communication recovered
EVENT Controller state=3 alarm=6
Sensor stale; communication invalid
```

또 다른 로그에는 다음과 같은 fragment가 있었다.

```text
Invalid SENSOR line: OOR:OPEN
```

이 로그는 재연결 시 UART stream의 중간 fragment가 한 줄로 해석되거나, 충분한 정상 sensor frame을 받기 전에 communication recovered가 선언되는 race 가능성을 보여줬다.

### ChatGPT의 분석

ChatGPT는 이를 다음 세 요소로 분해했다.

1. **UART stream fragment**  
   reconnect 직후 line boundary 중간부터 읽을 수 있음.

2. **Premature communication recovered**  
   일부 수신만으로 통신 정상으로 판단하면 안 됨.

3. **Sensor stale watchdog race**  
   recovery 처리와 stale timeout이 짧은 시간 안에 서로 충돌할 수 있음.

이 문제는 실제 사용자의 재현 로그로 발견된 edge case이며, 당시 완전히 해결됐다고 과장하지 않고 후속 수정 대상으로 구분한다.

## 11 STATUS 1초 출력과 이벤트 로그

### 사용자의 요구

사용자는 runtime에서 상태가 매우 자주 출력되는 문제를 지적했다.

> 발언 요지: “상태도 지금 엄청 많이 출력되는데 1초에 한 번 출력되는 걸로 할 수 있나?”

이 요구는 Controller의 제어 주기를 느리게 하라는 뜻이 아니라 **콘솔 표시 주기만 줄이려는 요구**였다.

### ChatGPT의 구체화

ChatGPT는 다음처럼 제어와 로깅을 분리했다.

```text
Control / Sensor / Interlock polling
→ 기존 빠른 주기 유지

Periodic STATUS
→ 1초에 한 번

Critical event log
→ 즉시 출력
```

즉 E-Stop, ERROR, RESET, START, RUN complete, UART disconnect/reconnect, state transition 같은 이벤트는 1초를 기다리지 않고 바로 출력하는 방식이다.

### Codex의 구현

1초 STATUS 출력과 관련 runtime 변경이 Codex 작업으로 보고됐다.

## 12 `./run`과 실행 기준선 관리

### 사용자의 요구와 문제 확인

사용자는 프로젝트 실행을 `./run`으로 단순화하길 원했고, 실제 장비 동작을 반복 검증할 때도 이 진입점을 사용했다.

개발 과정에서는 최신 소스가 build됐는데 `./run`이 다른 build 경로의 오래된 바이너리를 실행하는 문제가 확인된 기록이 있다. 이 경우 소스에는 READY·STATUS 관련 변경이 있어도 실제 장비에서는 이전 동작이 보일 수 있다.

이 문제는 “코드가 없다”와 “최신 코드가 실행되지 않는다”를 구분해야 하는 대표 사례다.

### ChatGPT의 역할

ChatGPT는 소스만 보는 것이 아니라 다음을 함께 확인해야 한다고 정리했다.

- `run` script가 가리키는 binary path
- 현재 build directory
- executable timestamp
- current Git HEAD
- 실제 runtime log

### 사용자 역할

사용자는 실제 `./run` 실행 결과를 제공하고, 예상 동작과 다른 경우 즉시 문제를 제기했다. 최종적으로 실행 기준선과 Git 기준선이 일치하는지 확인하는 주체도 사용자였다.

## 13 Git 기준선과 검증 책임

### 사용자 직접 수행

프로젝트 운영 규칙상 Codex나 ChatGPT가 Git commit/push를 대신하지 않았다. 사용자는 검증된 기능 단위마다 직접 commit하고, 마지막에 `git push origin main`을 수행했다.

대화에서 전달된 예시 기준선에는 다음과 같은 push 결과가 포함됐다.

```text
To https://github.com/triton0305/equipment-controller.git
   4868228..b2217e9  main -> main
```

따라서 최종 기준선을 GitHub에 올린 주체는 사용자다.

### ChatGPT의 역할

ChatGPT는 다음을 지원했다.

- 어느 시점이 의미 있는 commit 단위인지 판단
- commit title/message 추천
- Host Test와 HW 검증 상태 구분
- 인수인계에 현재 HEAD·검증 수준 정리

### Codex의 역할

Codex는 소스와 테스트를 수정하지만 commit/push는 수행하지 않는 운영 원칙을 유지했다.

## 14 ChatGPT의 분석과 사용자 피드백에 따른 수정

ChatGPT는 상태 머신, safety, communication, sequence를 구조화했지만 항상 처음부터 정확했던 것은 아니다. 사용자가 실제 장비와 코드 결과를 확인하면서 설명을 여러 차례 수정했다. 이 피드백도 사용자 기여로 기록한다.

| 항목 | ChatGPT의 초기 설명 또는 가설 | 사용자의 확인·수정 | 최종 정리 |
| --- | --- | --- | --- |
| READY 5초 | 실제 동작이 안 보여 미구현 가능성을 빠르게 언급 | “5초 조건 왜 구현 안 됐어”라고 실제 동작 기준으로 재확인 | 코드 존재 여부와 실행 binary 기준선을 분리 |
| START 경로 | `main.cpp`에서 runtime command를 우회할 가능성 검토 | 실제 코드 확인 | `runtime.handleCommand()` 경로 정상, 가설 배제 |
| command case | START 입력을 설명하며 대소문자 조건을 먼저 강조하지 못함 | 소문자 `start`가 동작하지 않아 사용자 지적 | case-sensitive 원인 확인, 이후 case-insensitive 개선 |
| Sequence | 출력이 켜지는 것 자체를 먼저 설명 | 사용자가 “한 번에 전부 켜지는 게 맞나” 지적 | 단계 진행 문제로 확대해 one-poll-one-step 수정 |
| Critical range | rough normal range와 progression 후보를 제안 | 사용자가 critical과 process 조건을 분리하고 progression은 보류 | critical만 확정, progression target은 후속 |
| RGB/개별 LED | 초록 LED를 READY/RUN 표시로 단순화한 설명이 있었음 | 사용자가 RGB와 별도 초록·노랑 LED 구성을 다시 설명 | RGB=state, 별도 LED=Pump/Heater로 역할 분리 |
| UART reconnect | 통신 복구가 구현돼 있다는 기준선 중심 설명 | 사용자가 fragment/stale race 실제 재현 | edge case를 별도 미해결 항목으로 분리 |

이 표는 ChatGPT의 가설과 실제 확정 사실을 섞지 않기 위한 기록이다. 사용자는 설명이 장비의 실제 의도와 맞지 않을 때 반복적으로 수정했고, ChatGPT는 그 피드백을 반영해 명세와 검증 기준을 갱신했다.

## 15 대표적인 실제 협업 흐름

### Sequence 이상

사용자:

> “보통 한 번에 전부 다 켜지는 게 맞아?”

실제 로그 제공:

```text
SequenceStep=7
Pump=1
Heater=1
```

ChatGPT:

```text
sensor update와 sequence transition 결합 가능성 분석
→ one-poll-one-step 명세
→ 단계별 공정 흐름 정리
```

Codex:

```text
sequence progression 수정
→ regression test
→ CTest
```

사용자:

```text
실제 장비에서 Pump/Heater 단계 동작 재확인
```

### DHT11 unavailable

사용자:

> “왜 unavailable임?”

ChatGPT:

```text
Linux STATUS
→ raw UART
→ WSL USB
→ STM32 production firmware
```

사용자:

```text
명령 실행
→ USB 연결 확인
→ production firmware Flash
→ TEMP/HUMIDITY raw data 확보
```

Codex:

```text
production path와 SensorState 구현
```

### Critical Safety

사용자:

> “임계치를 넘어가면 E-STOP으로 넘어가게. 임계치는 진짜 비정상적이어야 함.”

ChatGPT:

```text
Critical Safety와 Process Progression 분리
→ threshold 후보 정리
→ Alarm/ERROR 처리 명세
```

사용자:

> “50도 이상으로 하자.”

Codex:

```text
critical sensor fault 구현
→ regression test
```

사용자:

```text
가변저항·센서값으로 실제 동작 확인
```

### Door re-arm

사용자:

> “다시 바로 시작하는 건 아니지? 문 다시 열었다가 닫아야지?”

ChatGPT:

```text
RUN complete
→ WaitOpen
→ WaitClose
→ 5s qualification
→ READY
```

Codex:

```text
re-arm state 구현
→ Host test
```

사용자:

```text
실제 Door OPEN/CLOSE로 cycle 재진입 검증
```

## 16 검증 수준을 구분한 역할

이 프로젝트에서는 구현과 검증을 같은 의미로 사용하지 않는다.

| 상태 | 의미 | 주체 |
| --- | --- | --- |
| IMPLEMENTED | 코드 변경 완료 | Codex 작업 결과 확인 |
| BUILD VERIFIED | Host/STM32 target build 성공 | Codex 또는 사용자 환경 |
| HOST VERIFIED | CTest/regression test 통과 | 자동화 테스트 결과 |
| HW VERIFIED | 실제 보드·센서·출력에서 동작 확인 | 사용자 실물 관찰 필수 |
| HW FAILED / NOT VERIFIED | 실제 장비에서 기대 동작을 확인하지 못함 | 사용자 실험 + ChatGPT 판정 보조 |
| PENDING | 정책 또는 검증 미완료 | 후속 작업 |

ChatGPT는 테스트 통과 수치를 그대로 HW VERIFIED로 해석하지 않았다. 예를 들어 DHT11 production path는 Host Test가 통과한 뒤에도 사용자가 실제 UART에서 `TEMP`와 `HUMIDITY`를 확인한 후에야 HW 확인 근거가 생겼다.

대화에서 CTest 기준선은 개발 단계에 따라 증가했다. 초기 인수인계에는 41/41 PASS가 기록됐고, 후속 개발에서 44개 이상으로 늘어난 regression suite가 보고됐다. 테스트 개수 자체보다 중요한 것은 **어떤 기능이 Host에서 검증됐고 어떤 기능이 실물에서 확인됐는지 구분하는 것**이다.

## 17 사용자의 실제 의사 결정과 판단 근거

| 주제 | 이유 또는 조건 | 사용자의 제안과 행동 | ChatGPT의 구체화 |
| --- | --- | --- | --- |
| READY 5초 | Door CLOSED 한 번만으로 준비 완료를 선언하기 어려움 | 5초 연속 안전 확인 요구 | qualification timer와 reset condition |
| RUN 30초 | 한 사이클의 운전 시간을 명확히 제한 | 한 sequence 30초 확정 | RUN timer와 sequence 종료 구분 |
| Pump/Heater 순차 동작 | START 직후 두 출력이 동시에 켜지는 동작이 부자연스러움 | 실제 로그를 보고 문제 제기 | one-poll-one-step과 단계 흐름 |
| Door re-arm | RUN 종료 후 즉시 재시작을 막고 새 cycle을 명확히 구분 | Door OPEN 후 다시 CLOSE 요구 | WaitOpen/WaitClose 상태 구조화 |
| Critical Safety | 사람이 보기에도 명백한 센서 이상은 자동 정지 필요 | 극단적 threshold 요구, 50°C 상한 확정 | Critical/Reset/Progression 조건 분리 |
| RESET | 위험 원인을 없앴다고 자동 재가동되면 안 됨 | 안전 확인 후 B1 RESET 요구 | ERROR latch와 reset guard |
| STATUS 1초 | 빠른 상태 출력이 실제 관찰을 방해 | 주기 로그를 1초 간격으로 요구 | control cadence와 logging cadence 분리 |
| DHT11 문제 | 실제 STATUS가 unavailable | raw UART·Flash·재실행 수행 | 계층별 원인 추적 |
| UART reconnect | 실제 재실행에서 fragment/stale 문제 발생 | 로그 제공 및 문제 재현 | reconnect race 분석 |
| Git | 검증된 기준선만 저장 | 직접 commit/push | 시점·메시지·검증 상태 정리 |
| Progression target | Critical threshold와 공정 목표를 성급히 섞고 싶지 않음 | 후속 작업으로 보류 | 미확정 상태 명시 |

## 18 실제 책임 경계

### 사용자

사용자는 **시스템 요구사항·제어 정책의 최종 결정자이자 Hardware Operator, Final Validator** 역할을 맡았다.

사용자가 책임진 범위는 다음과 같다.

```text
장비가 어떻게 동작해야 하는지 결정
기능의 우선순위와 보류 여부 결정
Door / E-Stop / B1 / 가변저항 등 실제 조작
STM32 Flash와 UART/WSL 실행
실제 로그와 물리 동작 확인
HW 검증 합격 여부 판단
Git commit / push
최종 기준선 승인
```

### ChatGPT

ChatGPT는 **Design Review, Troubleshooting, Specification, Validation Planning** 역할을 맡았다.

```text
사용자 요구를 상태 머신과 조건으로 구조화
기술 대안과 충돌 가능성 설명
로그와 레지스터·통신 결과 분석
원인 후보를 계층별로 분리
Codex가 구현할 명세와 regression 조건 작성
Host Test와 HW VERIFIED 구분
인수인계와 검증 보고서 작성
잘못된 가설은 실제 확인 후 배제
```

ChatGPT는 장비를 직접 조작하거나 실제 하드웨어 결과를 만들어내지 않았다.

### Codex

Codex는 **Implementation과 Automated Regression Testing** 역할을 맡았다.

```text
C++ / STM32 source 수정
READY / RUN / Sequence / Safety / RESET 구현
Host regression test 작성
Build / CTest
관련 문서 변경
```

프로젝트 운영 원칙상 Codex는 Git commit/push를 수행하지 않았다.

## 19 포트폴리오와 면접에서의 정확한 표현

사용자의 역할을 과장 없이 설명하면 다음과 같다.

> Equipment Controller의 장비 운용 시나리오와 안전 정책을 정의하고, 실제 STM32 기반 하드웨어에서 Door·E-Stop·센서·Reset·RUN sequence를 직접 검증했습니다. 실제 실행 로그를 바탕으로 READY qualification, sequence progression, Door re-arm, critical sensor fault와 UART reconnect 문제를 식별했고, AI와 수정 방향 및 검증 기준을 구조화했습니다. 구현 결과는 Host regression test와 실제 HW 검증을 구분해 확인하고 Git 기준선을 직접 관리했습니다.

ChatGPT의 역할은 다음과 같다.

> 사용자 요구와 장비 로그를 바탕으로 상태 머신·안전 정책을 구조화하고, 문제 발생 시 Controller–UART–WSL USB–STM32 firmware 계층으로 원인을 좁혔습니다. 수정 요구사항을 Codex가 구현 가능한 명세와 regression test 조건으로 변환하고, Host PASS와 HW VERIFIED를 구분해 검증을 지원했습니다.

Codex의 역할은 다음과 같다.

> 확정된 명세를 바탕으로 C++/STM32 소스와 자동화 테스트를 수정하고 Build·CTest를 수행했습니다.

## 20 역할과 설계 결과

사용자는 실물 장비가 자연스럽고 안전하게 동작하는지를 기준으로 설계 정책을 판단했다. 단순히 기능이 코드에 존재하는지를 보는 대신, Door를 닫은 뒤 실제 READY까지의 시간, START 이후 Pump/Heater의 단계 진행, RUN 30초 종료, E-Stop과 RESET, DHT11·Pressure 상태, UART 장애와 복구를 실제 장비 행동으로 확인했다.

ChatGPT는 사용자의 판단을 FSM, timer, interlock, sequence, communication recovery, validation rule로 구체화했다. 잘못된 초기 가설이나 과도한 단정은 사용자의 실제 로그와 코드 확인을 통해 수정했고, 최종 사실과 조사 중 가설을 분리했다.

Codex는 확정된 요구와 명세를 실제 소스 및 회귀 테스트로 구현했다. 다만 코드 구현과 Host Test 성공 자체를 최종 HW 검증으로 간주하지 않고, 사용자의 실물 확인을 마지막 단계로 유지했다.

이 프로젝트의 실제 협업 구조는 다음과 같다.

```text
사용자
= Requirements / Control Policy
+ Hardware Operation
+ Final Validation
+ Git Baseline

        ↓ 요구·판단·실제 로그

ChatGPT
= Design Review
+ Troubleshooting
+ Specification
+ Validation Planning

        ↓ 구현 명세

Codex
= Implementation
+ Build
+ Automated Regression Test

        ↓ 변경 결과

ChatGPT 검토
        ↓
사용자 실제 HW 검증
        ↓
사용자 최종 승인 / Commit / Push
```

가장 중요한 기여는 단순히 AI를 사용해 코드를 생성한 것이 아니라, **사용자가 실제 장비 동작의 목적과 안전 기준을 결정하고, AI를 설계 검토·원인 분석·명세화 도구로 사용한 뒤, 실물 결과로 다시 판단했다는 점**이다.
