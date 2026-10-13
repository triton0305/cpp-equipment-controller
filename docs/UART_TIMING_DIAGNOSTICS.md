# UART startup/reconnect timing diagnostics (개발 15)

이 계측은 원인 식별용이다. SensorTimeout=1000ms, sync pair, Error latch,
READY/START, 공정 및 안전출력 정책은 변경하지 않는다. HW 결과는 아직 없다.

## 실행

```bash
cmake --build build -j 4
mkdir -p /tmp/equipment-uart-diag
EQUIPMENT_UART_DIAG=/tmp/equipment-uart-diag/run-01.csv ./run
```

일반 STATUS/EVENT는 기존 terminal에 그대로 출력된다. 15~30초 관찰한다.
fault가 발생하면 자동 reconnect와 정상 telemetry 회복까지 관찰한 뒤 Ctrl+C 한다.
안전 종료 및 UART close 후 `UART_DIAG saved: ...`가 나오며 CSV가 생성된다.
재실행할 때 파일명을 `run-02.csv`, `run-03.csv`로 바꿔 이전 결과를 보존한다.
포트가 다르면 `./run /dev/ttyACM1`처럼 기존 인자를 사용한다.

```bash
python3 tools/uart_diag_summary.py /tmp/equipment-uart-diag/run-*.csv
```

환경변수 없이 `./run`하면 recorder는 생성되지 않는다. 실행 중에는 CSV 파일을
열거나 쓰지 않으므로 파일이 아직 없는 것은 정상이다. SIGKILL/crash/power loss에는
메모리 기록을 내보낼 수 없다. 부모 디렉터리는 미리 만들어야 한다. save는 같은
파일을 덮어쓰므로 매 실행마다 고유 이름을 사용한다.

## A/B/C/D 비교

- A: session=1에서 recovery 후 fault 없음.
- B: session=1에서 `recovered_log_begin` 뒤 `timeout_fault` 발생.
- C: 같은 실행의 session>=2. sync 소요시간, read/write/poll 지연을 session=1과 비교.
  진단의 session은 UART open 성공 횟수다. 이 번호는 Error latch 해제를 뜻하지 않는다.
- D: Ctrl+C 후 새 CSV를 만든 재실행. A/B 여부를 판정하고 앞 실행과 비교.

정상 STATUS의 ControllerState/Alarm 값과 CSV 측정값을 함께 보관한다. 요약 도구는
측정 수치만 출력하며 실제 HW 원인을 자동 확정하지 않는다.

## CSV 필드와 이벤트

모든 시간은 Linux `std::chrono::steady_clock`이다. `monotonic_us`는 해당 clock epoch의
절대 microseconds이며 나머지 시간 필드는 소수점 3자리 milliseconds다.

- `sequence`, `elapsed_ms`: 기록 순서 및 바로 이전 **발생 이벤트**와의 시간 차.
- `phase`: Disconnected / Synchronizing / Operational. transport 내부에서는 Runtime이
  마지막 제공한 context를 사용하며 fd 오류는 이후 Runtime fault/disconnect에서 반영된다.
- `session`, `poll`: open 성공 및 poll 순번.
- `last_sensor_age_ms`, `sync_age_ms`: 기록 시점에서 해당 Runtime timestamp까지의 나이.
  이전 session의 last_sensor도 식별 가능하게 남긴다. 한 번도 설정되지 않았으면 -1.
- `recovery_age_ms`: 해당 session의 recovered 로그 출력 시작부터 경과시간. sync 시작 시 초기화.
- `rx_events`, `rx_index`: 이번 batch의 전체 event 수와 현재 처리 예정인 1-based index.
  poll 시작 시 0. `rx_event`는 timeout 검사 **전**이므로 실제 적용 여부는
  `last_sensor_updated`로 확인한다. `rx_bytes`는 아직 framing 전이므로 count가 0일 수 있다.
- `bytes`: read 종료/`rx_bytes`는 실제 read byte 수, write 시작은 요청 수,
  write 종료는 성공적으로 넘긴 수. `timeout_rx_queue`는 FIONREAD 결과(-1은 조회 실패).
- `result`: read/write/send 종료 및 queue 조회의 성공 1/실패 0, 해당 없으면 -1.
- `duration_ms`: 종료 event의 해당 함수/구간 소요시간. 해당 없으면 -1.
- `poll_gap_ms`, `max_poll_gap_ms`: poll **진입→진입** 간격과 프로세스 lifetime 최대값.
- `detail_hex`: RX/line/reason의 최초 48 bytes를 hex로 기록. 전체 raw stream 덤프는 아니다.

구간 기록: `poll_begin/end`, `service_begin/end`, `read_events_begin/end`,
`send_outputs_begin/end`, `write_begin/end`, `status_begin/end`.
조기 return도 RAII로 종료 기록을 남긴다. 중첩 구간 소요시간을 합산하지 않는다.

주요 event: `open_begin/end`, `sync_begin`, `door_received`, `sync_estop_received`,
`sync_complete`, `estop_received`, `recovered_log_begin/end`, `last_sensor_updated`,
`rx_bytes`, `rx_event`, `rx_oversized`, `invalid_sensor`, `timeout_fault`,
`timeout_rx_queue`, `communication_fault`, `disconnected`, `shutdown_complete`.

`sync_estop_received`는 sync를 완성한 ESTOP, `estop_received`는 일반 처리 경로다.
같은 sync ESTOP에 두 기록이 있을 수 있으며 이때 수신 횟수를 2회로 세면 안 된다.
`last_sensor_updated` 시각과 sensor age는 다를 수 있다: 기존 정책대로 실제 저장값은
batch의 `received_at`이며 계측을 위해 새 timestamp로 바꾸지 않는다.
`timeout_fault`는 **실제 timeout 비교에 사용한 now**를 기록한다.

## Sensor stale 분류

1. `timeout_fault.recovery_age_ms`: recovered→fault 시간.
   `last_sensor_age_ms`는 실제 판정에 사용한 last_sensor의 나이. Synchronizing fault이면
   `sync_age_ms`가 기준이다. Operational stale은 1000ms 이상이어야 한다.
2. `write_end.duration_ms`가 크고 `send_outputs_end`도 크면 출력 지연 경로.
   result=1도 확인: 성공한 느린 write와 UART write 실패를 구분한다.
3. `read_events_end.duration_ms`가 크면 read/framing 구간 지연.
4. poll 진입 간격은 큰데 직전 `poll_end.duration_ms`가 작으면 poll 바깥
   스케줄링/기존 main 처리 공백을 의심한다. 이것만으로 OS 스케줄러 원인을 확정하지 않는다.
5. `status_end.duration_ms` 또는 `recovered_log_begin/end` 사이 차이가 크면 기존 로그
   출력 지연을 확인한다. recorder는 일반 STATUS 기록 정책을 바꾸지 않는다.
6. read/poll이 정상 주기로 계속되지만 RX bytes가 0이고 sensor update가 중단되면
   Linux read 관점의 수신 공백. bytes는 있는데 invalid/oversized만 있으면 framing/내용 문제.
7. timeout 당시 kernel queue>0이면 미처리 입력이 있다. 그러나 bytes의 나이/유효성은
   알 수 없다. queue=0도 현재 batch에 미처리 event가 없다는 뜻은 아니다. rx_index와
   rx_events, 마지막 sensor update를 같이 본다.

Linux 측 read 기록만으로 MCU가 실제 송신을 멈췄는지, USB/VCP 중간에서 유실됐는지는
구분할 수 없다. 이 경우 NRST/TX logic analyzer 또는 별도 OS tracing이 필요하다.

## 오버헤드와 보존 한계

고정 262144개 레코드를 UART open 전에 할당/초기화한다. 앞 1/4(65536개)는 최초
기록으로 고정하고, 나머지는 최근 기록을 순환 보존한다. CSV 첫 줄의 `record_bytes`
× `capacity`가 record storage 크기다. overflow 시 `dropped` 및 sequence gap으로 표시한다.
최대 poll gap은 lifetime 값이지만 다른 duration 최대/합계는 보존된 records의 값이다.
따라서 오래 실행하지 말고 startup/reconnect 관찰 직후 Ctrl+C 한다.

활성 계측은 clock 조회와 고정 메모리 복사만 수행한다(기록 중 동적 할당/문자열 포맷/
파일·terminal 출력/lock 없음). 예외는 timeout 시 진단용 **FIONREAD ioctl 1회**이며
입력을 소비하지 않는다. 일반 로그의 기존 I/O는 그대로다. CSV 포맷/쓰기 비용은 안전
종료 후 발생한다. 관측 오버헤드는 0이 아니며 deadline 가까이서는 결과에 영향을 줄 수 있다.
실제 HW에서 계측 ON/OFF의 발생률도 비교해야 한다. 기존 10ms main loop sleep 이외에
새 sleep이나 deadline 완화는 추가하지 않았다.

## 검증 상태

Host build 및 Full CTest 46/46 PASS(기존 45개 + LINUX_UART_DIAGNOSTICS_HOST 1개).
새 테스트는 deadline/age, successful slow write, reconnect session, 조기 return의
begin/end 짝, bounded buffer wrap 및 export 실패를 검증한다. 실제 Linux executable을
PTY에 연결한 정상/수신공백→자동 reconnect 시험에서도 Ctrl+C 뒤 CSV 저장을 확인했다.
이 Host 빌드의 record 크기는 192 bytes이므로 기본 storage는 48MiB다.
새로운 NUCLEO-F411RE HW 측정은 수행하지 않았다.
