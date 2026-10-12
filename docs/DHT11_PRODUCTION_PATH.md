# DHT11 운영 경로 — 2026-10-12

**구현 완료 / Host 44/44 PASS / STM32 production build PASS / UART/STATUS 사용자 관찰 확인, 추가 HW 검증 대기**.
후속 [Cycle Safety 정책](CYCLE_SAFETY_POLICY.md)에서 sensor critical 판정과 실제값 준비 대기를 추가했다.
사용자가 UART 수신과 Linux 실제값 표시를 확인했으며, DHT 오류/타이밍의 추가 HW 항목은 대기 중이다.

기존 작업 트리 변경을 유지했으며 commit, push, flash는 수행하지 않았다.

## 실제 데이터 경로

Temperature:
`PC4 DHT11 → Dht11_Poll → IoEnvironment → IoProtocol_Serialize → TEMP:<°C> → UART → AsciiProtocol → SensorState.temperature/temperature_valid → 1초 STATUS Temperature`.

Humidity:
`PC4 DHT11 → Dht11_Poll → IoEnvironment → IoProtocol_Serialize → HUMIDITY:<%RH> → UART → AsciiProtocol → SensorState.humidity/humidity_valid → 1초 STATUS Humidity`.

운영 main은 기존 TIM5 초기화 후 `IoEnvironment_Init(&htim5)`를 호출한다.
DHT11 독립 harness 및 Stepper 독립 빌드는 운영 adapter를 활성화하지 않는다.
기존 PC4/TIM5/CubeMX 설정은 변경하지 않았다. main.c의 USER CODE 영역만 연결했다.

정상 값 예시 (실물 측정 결과가 아닌 프로토콜 예):

```text
DOOR:CLOSED
ESTOP:OFF
PRESSURE:2048
TEMP:25.9
HUMIDITY:45.2
```

기존 Door/E-Stop/Pressure 순서 및 100ms 출력은 유지한다. 온습도는 새 측정 결과가 있을 때만
그 뒤에 추가한다. B1 RESET 요청이 있으면 snapshot 끝에 붙인다.
온습도 문자열은 정수 0.1 단위로 직렬화하며 음수 소수(-0.1°C 포함)를 보존한다.
기존 DHT11 decoder의 checksum/decimal/range 검증을 통과한 값만 전송한다.

Linux TEMP는 기존처럼 유한한 온도 숫자를 수용한다. HUMIDITY는 0..100% 범위를 검사한다.
운영 DHT11의 범위 검사는 기존 decoder 기준(-20..60°C, 5..95%RH)이다.
NaN/Inf, 빈 값, 숫자 뒤 잡문자, 비십진 표기는 거부하고 해당 validity를 해제한다.
다른 센서 필드에는 영향을 주지 않는다.

## Freshness / 실패 정책

- 시작 직후 유효 측정값은 없다. 기존 드라이버의 2.1초 안정화 및 2.1초 최소 측정 간격을 유지한다.
  20ms START LOW는 cooperative이고, pulse capture는 기존 최대 6ms 제한을 유지한다.
- 첫 값 수신 전 snapshot은 Temperature/Humidity 모두 `unavailable`이다.
- 초기화 실패, response/data timeout, checksum/range 실패 시
  `TEMP:INVALID`와 `HUMIDITY:INVALID`를 전송한다. 숫자 0, 임의 기본값, 이전 측정값으로 대체하지 않는다.
- 측정 실패 후 드라이버 cooldown을 거쳐 다음 실제 성공값으로 복구한다.
  초기화 자체가 실패하면 adapter는 비활성 상태이며 재초기화가 필요하다.
- 성공값은 한 번만 전송한다. 100ms 센서 refresh에 이전 값을 반복해서 넣어 freshness를 연장하지 않는다.
- STM32는 마지막 성공 측정 후 **5,000ms 이상**이면 무효화한다. UART TX가 지연되면
  오래된 값이나 이후 실패로 무효화된 미완료 packet도 폐기한다. 부분 숫자가 정상 값으로
  끝나지 않도록 `!\n`으로 해당 line을 무효화한 뒤 INVALID 두 line을 보낸다.
- Linux는 Temperature/Humidity 각각 마지막 정상 수신 시각을 steady_clock으로 추적한다.
  **5.000초 이상**이면 해당 validity를 해제한다. 다른 센서 수신은 온습도 수명을 연장하지 않는다.
- malformed 값 또는 INVALID는 해당 값을 즉시 무효화하며 공통 통신 timeout을 갱신하지 않는다.
  UART fault/disconnect는 두 validity를 모두 폐기한다. 재연결 후 새 정상 수신이 필요하다.
- `SensorState`의 마지막 숫자는 진단용으로 남지만 validity=false이면 측정값으로 사용할 수 없다.
  snapshot은 이를 `unavailable`로 표시한다. humidity의 초기 0과 기존 temperature 기본 25°C를
  실제 수신값으로 표시하지 않는다.
- DHT 연결 단계에서는 측정 경로와 유효성 처리만 추가했다. 이후 Cycle Safety 변경에서
  temperature_valid에 따른 준비 대기, 센서별 2회 critical ERROR와 유효 정상값 RESET guard를 추가했다.
  현재 운전 정책은 위 Cycle Safety 문서를 따른다.

## UART / fail-safe 보존

DHT pulse capture 중 짧은 foreground hook이 UART hardware byte를 128-byte queue에 저장한다.
명령 parsing/TX 또는 blocking 호출은 hook에서 수행하지 않는다. 115200 baud에서 6ms 동안의
최대 약 70 byte를 수용한다. overflow/hardware RX 오류는 queue를 폐기하고 기존 runtime의
fragment discard 경로에 오류를 전달한다. IRQ/DMA/GPIO 설정 변경은 없다.

control timeout은 capture 직전과 직후, RX parsing 전에 검사한다. capture 중 deadline을
넘긴 queued ON 명령이 기존 failsafe latch를 해제할 수 없는 Host test를 추가했다.
PUMP → HEATER → VALVE → BUZZER → RUN_LED → ERROR_LED 및 후속 STATE는 변경하지 않았다.

실제 hook 실행 시간, DHT pulse timing, UART ORE 발생 여부 및 물리적 fail-safe 응답 시간은
운영 firmware의 실물 확인 항목이다. Host fake가 실제 peripheral timing을 입증하지는 않는다.

## 검증 결과

- 작업 전 전체 build / CTest: **42/42 PASS**.
- 최종 전체 build / CTest: **44/44 PASS**, 10.52초.
- 새 `STM32_DHT11_RUNTIME_HOST`: 초기 무효, 성공 측정 직렬화 및 Linux parsing, 오류 종류별 처리,
  복구, cached 값 재전송 금지, tick wrap, stale timeout, TX backpressure/부분 frame 폐기,
  capture를 가로지르는 command-loss deadline, 초기화 실패.
- 새 `LINUX_DHT11_RUNTIME_HOST`: 온도/습도 valid/invalid parsing, 실제 STM32 serializer → PTY →
  Linux SensorState → 1초 snapshot, 4.999/5.000초 경계, 필드별 freshness, INVALID,
  잘못된 숫자, disconnect/reconnect, 실패 보고만으로 통신 timeout 갱신 금지.
- 기존 `STM32_DHT11_DRIVER_HOST`: 정상 waveform을 UART service hook(가상 2µs)과 함께 decode.
- 기존 `STM32_PROTOCOL_CONTRACT`: 음수 소수, 출력 용량, decoder 범위와 invalid 직렬화.
- 기존 `STM32_UART_ADAPTER_HOST`: capture queue 순서/wrap/overflow/RX 오류/복구.
- TEST_28 포함 기존 42개 및 command-loss fail-safe / READY / RUN / RESET 테스트 모두 PASS.
- STM32 `Debug`, `Dht11Hardware`, `StepperHardware` build 모두 PASS.
  Production Debug: RAM **2,424 B**, FLASH **23,680 B**.
- 기존 main.c의 CRLF를 유지했다. whitespace 확인은
  `git -c core.whitespace=cr-at-eol diff --check`로 수행한다.

재실행 명령:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
cd firmware/stm32-equipment-io
cmake --preset Debug -DSTEPPER_HARDWARE_TEST=OFF -DDHT11_HARDWARE_TEST=OFF
cmake --build --preset Debug
```

## 이번 작업의 변경 파일

| 영역 | 파일 |
|---|---|
| 운영 DHT adapter | `App/Inc/io_environment.h`, `App/Src/io_environment.c` (새 파일) |
| Firmware 연결 | `Core/Src/main.c`, firmware `CMakeLists.txt`, `App/Src/io_runtime.c` |
| DHT capture hook | `App/Inc/dht11.h`, `App/Src/dht11.c` |
| UART capture queue | `App/Inc/uart_io.h`, `App/Src/uart_io.c` |
| MCU 센서 직렬화 | `App/Inc/io_protocol.h`, `App/Src/io_protocol.c` |
| Linux 데이터/파싱 | `modules/core/include/sensor_state.hpp`, `modules/protocol/src/ascii_protocol.cpp` |
| Linux freshness/snapshot | `apps/controller/controller_runtime.hpp`, `controller_runtime.cpp` |
| Tests | `tests/CMakeLists.txt`, `tests/transport/test_runtime_environment.cpp` (새 파일) |
| Firmware tests | `tests/firmware/test_io_runtime.cpp`, `test_io_protocol.cpp`, `test_dht11.c`, `test_uart_io.c` |
| 공용 HAL fake | `tests/firmware/fakes/stm32f4xx_hal.h`, `tests/firmware/dht11_fakes/stm32f4xx_hal.h` |
| 문서 | 이 문서, `docs/CONTROLLER_RUNTIME_POLICY.md`, firmware `HARDWARE_VERIFICATION.md` |

표의 App/Core 경로는 `firmware/stm32-equipment-io/` 기준이다.
기존 수동 변경 및 실행 중 발견된 `run` 파일 변경은 보존했다.

## 실제 HW에서 확인할 항목 — 전부 대기

1. Production Debug firmware에서 부팅 후 첫 실제 온도/습도 수신과 약 2.1초 주기 갱신.
2. UART TEMP/HUMIDITY 값과 Linux 1초 snapshot 값/소수 자리 일치.
3. DHT11 분리/불량 응답 시 INVALID → unavailable, fabricated 값·이전 값 재전송 없음.
4. 센서 정상 복귀 후 새 측정값으로 복구. 온습도 수신 중단 시 5초 만료.
5. DHT 측정 중에도 Door/E-Stop/Pressure 100ms 경로와 READY/START/RUN/RESET 동작.
6. 연속 측정 중 UART RX 오류 카운터/command refresh 상태 및 DHT checksum/timing 확인.
7. command-loss Pump/Heater OFF, stale RUN 제거, ERROR red 보존 및 safe refresh 재동기화 재검증.

기존 DHT11 독립 harness의 HW VERIFIED 및 사용자 제공 IDLE/READY/RUN 기록은 역사적 기록으로
유지한다. 후속 사용자 관찰에 따라 **UART 수신 및 STATUS 실제값 표시는 HW VERIFIED**로 기록했다.
그 외 오류/freshness/UART timing 항목 및 새 Cycle Safety 정책은 HW 미검증이다.

권장 commit message (미실행):
`feat: connect production DHT11 telemetry with validity and freshness tracking`
