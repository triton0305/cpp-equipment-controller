# Hardware Verification Report — 2026-10-08 (Asia/Seoul)

Firmware baseline: `0a73996`. No firmware, CubeMX, pin, clock, or Linux production
code changes were made in this session. No git add/commit/push/rebase/reset was run.


## Latest RUN-state E-Stop physical E2E — PASS

Supersedes the earlier E-Stop failure and E2E pending statuses recorded below.
The user explicitly confirmed all four physical effects: pump/heater LEDs OFF,
buzzer audible ON, ERROR red ON, followed by all OFF after the test's 3-second
observation window. Buzzer was reconnected for this requested test.

Actual sequence captured:

1. Real Door CLOSED, E-Stop OFF and pressure about 1820 allowed the unchanged
   Linux controller to enter RUN. Before fault, GPIOA ODR was `0x40` (heater ON),
   GPIOB ODR `0xA0` (pump and RUN green ON); matching IDR bits were observed.
2. Real UART `ESTOP:ON` was received, followed by
   `DECISION before=2 state=3 alarm=5` (RUN -> ERROR / EmergencyStop).
3. Actual Linux serializer emitted PUMP:OFF, HEATER:OFF, VALVE:CLOSED,
   BUZZER:ON, RUN_LED:OFF and ERROR_LED:ON.
4. Next GPIO snapshot had GPIOA ODR `0x80` and GPIOB ODR `0x10`; both ODR and
   IDR confirmed pump/heater OFF, buzzer ON, green OFF and red ON.
   Fifteen fault-window GPIO snapshots matched. A sixteenth captured the
   subsequent explicit test cleanup (all OFF), not a fault-output mismatch.
5. User physical observation confirmed the requested LED and buzzer behavior.
   Independent final GPIOA/GPIOB output readback was zero.

No production Controller, firmware, CubeMX, pin, clock or policy was changed.
The temporary harness linked existing Linux libraries. To provide an observable
RUN window it invoked the existing update() at a fixed 3-second test cadence,
with immediate dispatch on E-Stop assertion / Door opening. It never assigned
controller state or fault outputs. This verifies the physical functional path,
not production scheduling latency or a real-time emergency-stop response bound.
It issued an explicit all-off teardown after 3 seconds; the existing Controller
fault policy still keeps buzzer/error ON until reset outside this test teardown.
The first attempt faulted in READY and was correctly excluded from the RUN PASS.

Evidence: `run-estop-e2e.jsonl`, `run-estop-summary.json`,
`run-estop-final-off.log`, `run-estop-too-early.jsonl`, and temporary harness
`run_estop.cpp`, under `build/hardware-verification/2026-10-08/`.
The successful run logged one rejected initial line; zero-corruption startup
is not claimed. Pressure remains raw ADC; temperature/motor inputs use the
existing defaults and were not part of this targeted test.

## Earlier E-Stop retest — PASS for physical input

A subsequent 15-second input-only test received actual `ESTOP:OFF -> ESTOP:ON
-> ESTOP:OFF -> ESTOP:ON` transitions while the user operated the switch.
This supersedes the earlier failed E-Stop input attempts. The underlying reason for
the earlier constant HIGH was not established. EmergencyStop controller physical
E2E was NOT VERIFIED at that point; the latest RUN-state physical E2E test
supersedes that status with PASS / HW VERIFIED.
Evidence: `build/hardware-verification/2026-10-08/estop-quick.log`.
No output commands or firmware changes were made by this retest.

## Current results and exact evidence scope

| Item | Result | Evidence / limits |
| --- | --- | --- |
| WSL ST-LINK / VCP access | PASS | NUCLEO-F411RE identified, 3.26 V, `/dev/ttyACM0` opened at 115200 8N1 |
| Existing Flash contents | PASS | User performed Flash; independent SWD readback of 19,476 bytes exactly matches current ELF binary |
| Boot / foreground runtime | PASS | After user power cycle and WSL USB reattach, continuous actual DOOR/ESTOP/PRESSURE telemetry |
| PUMP PB7 | PASS at MCU pin | UART ON/OFF produced matching ODR and IDR transitions |
| HEATER PA6 | PASS at MCU pin | UART ON/OFF produced matching ODR and IDR transitions |
| BUZZER PA7 | PASS at MCU pin; audible ON confirmed | UART ON/OFF produced matching ODR and IDR; user confirmed audible ON and subsequent OFF in the latest RUN-state test after reconnecting the buzzer |
| RUN green PB5 / ERROR red PB4 | PASS at MCU pin | Both ON/OFF commands produced matching ODR and IDR transitions |
| External LED / RGB response | PASS for tested physical E2E | User confirmed pump/heater LEDs OFF, audible buzzer ON, ERROR red ON and subsequent all OFF in the latest RUN-state E-Stop test; IDLE/READY colors were not part of this test |
| Door PB0 input | PASS | Actual OPEN/CLOSED transitions, PB0 HIGH/LOW agrees with telemetry in stable samples |
| E-Stop PB1 input | PASS / HW VERIFIED | Subsequent physical switch operation produced OFF/ON transitions; latest RUN-state test received ESTOP:ON and triggered EmergencyStop |
| PA0 ADC raw values | PASS | Physical potentiometer movement produced minimum 0, maximum 4095, intermediate values (175 distinct values in primary input window) |
| USART2 actual bidirectional communication | PASS | Sensor telemetry reaches Linux; Linux commands cause corresponding actual GPIO IDR/ODR transitions |
| Existing Linux UART executable | PASS for real serial operation | 419 accepted sensor lines in its 10-second run; no UART read/write failure. Remained Idle as expected without START |
| Door-driven controller physical path | PASS through MCU pins; attached-output response observed | Real input -> actual Linux controller -> real UART -> actual GPIO. DoorOpen Fault disables pump/heater and enables buzzer/error red |
| E-Stop EmergencyStop E2E | PASS / HW VERIFIED | RUN -> actual ESTOP:ON -> Linux EmergencyStop -> pump/heater OFF, buzzer/error red ON; GPIO readback and user physical observation agree |
| Core MVP — core physical E2E | PASS / HW VERIFIED | Sensor -> STM32 -> UART -> unchanged Linux controller -> UART -> physical outputs verified, including RUN-state E-Stop; response-time guarantees and extended scope remain unverified |

No DHT11, Stepper rotation or OLED physical acceptance is claimed. ADC evidence
is raw counts, not physical pressure calibration. No oscilloscope or voltage-meter
measurement of pins was performed: IDR is the MCU's digital pin readback.

## Session details

Initial serial open succeeded but received no bytes. The first output attempt was
interrupted by the user's power cycle, yielding an I/O error; it is not counted as
an output PASS. The user reported a broken RESET button. Following reconnection,
Windows showed ST-LINK Shared but WSL had lost the device. Reattaching the existing
shared device with `usbipd attach --wsl --busid 3-4` restored access. Initial USB
node access restrictions were absent on the reattached node.

No reflash or firmware changes were required. Flash readback SHA-256:
`9f5281e14ad9ee39e1110cf0a0a11265c1cde4b58a958f63793886b50491d161`.
This hashes the 19,476-byte raw binary, not the ELF container.

A repeated output test sent each of PUMP, HEATER, BUZZER, RUN_LED and ERROR_LED
ON then OFF, with observations and SWD GPIO reads. All ten pin checks passed.
Sensor telemetry continued during this test.

The temporary E2E harness links the existing built Linux protocol, controller,
transport and logging libraries. It collects all three actual sensor fields,
issues test START/RESET requests, calls the unchanged controller, and transmits
its exact serializer output. It updates roughly every 100 ms and requests START
at most once per three seconds. It is a test harness, not a replacement for the
production CLI, which has no START path. Temperature and motor state remain the
existing Linux defaults; they were not physically measured.

The E2E run accepted 2,092 sensor lines and rejected one invalid line. Acquisition
logs also include truncated/garbled initial lines around port opening and buffer
flushes; parser rejection occurred, and error-free startup framing is not claimed.
The runtime's RX error counter read 1 before and after the tests. It does not prove
that every possible communication fault is handled.

The E2E log contains Idle, Ready, Run, and two DoorOpen Fault transitions. All 296
GPIO snapshots eligible for comparison (complete commands available and >30 ms
after the last recorded output line) matched Linux output bits in both ODR and IDR.
Both DoorOpen Fault samples had GPIOA ODR `0x80` and GPIOB ODR `0x10`: pump/heater
OFF, buzzer ON and ERROR red ON. This is the existing Linux fault policy, not the
all-off initialization safe state. VALVE commands remain recognized but unsupported.

The user initially removed the buzzer due to excessive volume. The following
input-only E-Stop checks kept all outputs OFF. At that stage GPIOA and GPIOB ODR
were zero and GPIOC ODR was `0x10` (DHT11 PC4 HIGH; stepper PC0..PC3 all LOW).
The buzzer was subsequently reconnected for the requested RUN-state E-Stop test;
the user confirmed audible operation. Final GPIOA/GPIOB readback after that test
again confirmed all tested outputs OFF.

## RESOLVED / SUPERSEDED — E-Stop wiring/identity

- Earlier observation: PB1 stayed HIGH and ESTOP stayed OFF during initial
  attempts; DOOR toggled during one retest. Those failed attempts remain in logs.
- Resolution evidence: the later physical input test produced ESTOP OFF/ON
  transitions, and the latest RUN-state test verified EmergencyStop through
  physical outputs. The earlier issue is no longer an active blocker.
- Root cause of the initial observation was not established; no wiring repair
  or firmware fix is claimed. No pin remapping or input polarity change was made.

## Evidence and remaining work

Local raw logs, register captures, readback binary, analysis script and temporary
harness sources/executable are preserved under
`build/hardware-verification/2026-10-08/` (ignored build artifacts, not staged).
Key files: `output-test.jsonl`, `input-test.jsonl`, `estop-test.jsonl`,
`linux-runtime.log`, `e2e.jsonl`, `summary.json`, `flash-readback.log`,
`final-state.log`. `output-interrupted.jsonl` preserves the interrupted first run.

Remaining limitations:

- Emergency-stop response-time guarantee: NOT VERIFIED. Test harness scheduling
  does not establish production real-time latency.
- Pressure calibration and physical units/scaling: undecided; only raw ADC
  endpoints and intermediate values are verified.
- Disconnect/stale-data safety policy and recovery: NOT VERIFIED. Observed
  power-cycle/re-attachment is not acceptance of cable-loss output safety or
  automatic reconnection.
- DHT11, Stepper rotation and OLED: NOT VERIFIED.
- RGB IDLE/READY colors and VALVE/Stepper mapping remain outside the tested path.

The previous 34/34 host test result is unchanged; tests were not rerun because
production code was not modified. The temporary C++ harness compiled successfully
with `-Wall -Wextra -Wpedantic`. Only documentation is proposed for manual commit.
