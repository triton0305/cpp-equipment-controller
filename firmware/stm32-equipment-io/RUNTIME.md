# STM32 runtime verification record

Baseline: `babd7c5`, NUCLEO-F411RE / STM32F411RET6.

## Scope and status

GPIO device layer, ADC reader, ASCII protocol, and foreground runtime are
IMPLEMENTED. Host tests execute the actual C implementations with fake HAL/I/O;
the protocol contract tests use the actual Linux parser and serializer.
These tests are HOST VERIFIED only. They do not establish physical timing,
voltage, sampling accuracy, UART delivery, boot behavior, or hardware safety.

- Flash: BLOCKED (execution environment has no USB device exposure).
- Boot: NOT VERIFIED.
- GPIO physical output, Door/E-Stop physical input, ADC physical values:
  NOT VERIFIED.
- USART2 VCP and Linux/STM32 physical E2E: NOT VERIFIED.
- Disconnect/reconnect, recovery, physical safe-state behavior: NOT VERIFIED.
- DHT11, Stepper rotation, OLED: not implemented by this runtime.

Do not repeat ST-LINK discovery until device exposure changes. No CubeMX, pin,
clock, initialization, IRQ or DMA configuration was changed. `main.c` calls
`IoRuntime_Init` after generated peripheral initialization and `IoRuntime_Poll`
inside the foreground loop, exclusively in USER CODE regions.

## Runtime

`device_io.c` is now part of the firmware target. Existing APIs remain intact.
Initialization invokes the existing all-off safe state (including stepper coils).
`DeviceIo_SetIndicators` adds direct green/red outputs for Linux's independent
RUN_LED/ERROR_LED bits; blue is off. The original four-state RGB API remains
available but the wire contract does not convey IDLE/READY state.

`pressure_adc.c` starts a single ADC1 conversion and checks EOC on subsequent
polls. Pending conversions have a 10 ms deadline using wrap-safe tick arithmetic.
The most recent successful sample is available while the next conversion is
pending. Start/stop failure or conversion timeout invalidates the sample.
No ADC configuration or calibration/scaling is introduced.

`uart_io.c` uses USART2 SR/DR foreground polling, with no waiting loops and no
HAL interrupt/DMA transfer active. RX errors consume DR after SR and invalidate
the current line. This adapter must remain the sole USART2 transfer owner.
`io_protocol.c` buffers at most 31 characters, accepts LF or CRLF, and discards
an oversized, binary or receive-corrupted line through the next LF. Commands are
executed only after a complete valid line. There is no callback-side control logic.

Each runtime poll processes at most 32 RX bytes and 32 TX bytes, services ADC,
and schedules sensor reports every 100 ms when the previous packet has drained.
TX backpressure does not stop RX/ADC; no unbounded queue grows. A pending packet
is retained until writable. Continuous foreground service is required: polling
has no interrupt RX ring, and long future drivers can cause overruns. Test this
at 115200 baud on hardware before adding blocking DHT11/OLED/Stepper work.

## Existing wire contract

No acknowledgements, heartbeat, new command names or new fault policy were added.

| Direction | Existing format | Runtime behavior |
| --- | --- | --- |
| STM32 to Linux | `DOOR:CLOSED` / `DOOR:OPEN` | PB0 LOW means CLOSED |
| STM32 to Linux | `ESTOP:ON` / `ESTOP:OFF` | PB1 LOW means ON |
| STM32 to Linux | `PRESSURE:<integer>` | ADC raw 0..4095; omitted if invalid |
| Linux to STM32 | `PUMP:ON/OFF` | PB7 |
| Linux to STM32 | `HEATER:ON/OFF` | PA6 |
| Linux to STM32 | `BUZZER:ON/OFF` | PA7 |
| Linux to STM32 | `RUN_LED:ON/OFF` | PB5 green |
| Linux to STM32 | `ERROR_LED:ON/OFF` | PB4 red |
| Linux to STM32 | `VALVE:OPEN/CLOSED` | Recognized, unsupported; no physical action |

Every message ends with LF. Slash notation in the table represents alternatives.
`io_runtime_rx_errors` and `io_runtime_unsupported_valve_commands` are debugger
counters, not new wire messages. TEMP and MOTOR reports are not fabricated.

## Integration limitations requiring follow-up

- PRESSURE is a raw ADC count, not a calibrated physical pressure. Linux's existing
  threshold is 50; agreeing on sensor units/scaling is required before physical
  control acceptance. Host tests establish syntax and raw-value transport only.
- VALVE has no specified mechanical position/direction mapping to the 28BYJ-48.
  All received VALVE commands are counted but cannot claim actuator completion.
- The existing Linux `apps/controller/main.cpp` never issues START and exits after
  ten seconds. Host integration tests invoke the existing controller START API;
  they do not claim the shipped executable can run a full operator sequence.
- Linux starts with default temperature/motor/communication values. Missing ADC
  reports leave its previous pressure unchanged; no sensor-validity wire format
  exists. Actual DHT11 measurements and stale-sensor handling remain unresolved.
- No heartbeat/disconnect timeout policy exists in the inspected UART runtime.
  STM32 retains its last output commands if Linux stops sending. A TX packet may
  also remain pending indefinitely, without blocking other service. No new
  automatic safe-state or reconnection policy was invented.
- Linux handles individual sensor lines and sends individual output lines; neither
  multi-field snapshots nor output batches are atomic under the existing contract.
- Linux EmergencyStop fault turns pump/heater off and alarm/red on. This differs
  intentionally from the all-off device initialization safe state: Linux decisions
  are applied unchanged. No Linux state machine is duplicated on STM32.

## Reproduction

From the repository root, use existing build directories:

```sh
cmake --build firmware/stm32-equipment-io/build/Debug --clean-first
cmake --build build-refactor -j2
ctest --test-dir build-refactor --output-on-failure
```

The original 30 CTest scenarios remain. Four additional suites:

- `STM32_ADC_HOST`: endpoint/midpoint values, no-ready timeout, tick rollover,
  invalid samples and HAL failure paths.
- `STM32_PROTOCOL_CONTRACT`: all 64 actual Linux output combinations; STM32 sensor
  packets accepted by actual Linux parser; fragmented/CRLF/invalid/oversized lines.
- `STM32_RUNTIME_HOST`: real GPIO mappings and safe state against fake HAL, bounded
  RX/TX, backpressure, RX error recovery, ADC failures and tick rollover; real Linux
  controller decisions for simulated E-stop, open door and low pressure inputs.
- `STM32_UART_ADAPTER_HOST`: register-access branch behavior only; simulated
  registers cannot reproduce SR/DR hardware side effects or baud timing.

Next hardware session: Flash/verify ELF, prove boot reaches the foreground loop,
then observe every GPIO channel, input polarity, ADC endpoints/midpoint, VCP
bidirectional traffic, and full physical controller behavior. Disconnect/reconnect
and recovery require an agreed policy and separate physical validation.

## Recorded validation (2026-10-08, Asia/Seoul)

- STM32 full clean build: PASS, FLASH 19,476 B / RAM 2,144 B (linker report).
- Linux build: PASS.
- Full CTest: 34/34 PASS (original 30 plus four STM32 host suites).
- Baseline comparison: generated `main.c` content outside USER CODE regions is
  byte-identical; original CRLF line endings preserved. No `.ioc` or generated
  peripheral configuration changes.
- Hardware statuses above remain unchanged. No physical PASS is claimed.
