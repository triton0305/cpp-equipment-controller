# Equipment Controller — Troubleshooting

실제 발생한 장애와 안전성·처리 효율을 위해 수정한 5개 사례를 증상 → 원인 조사 → 조치 → 검증 순서로 정리합니다. Host 자동 테스트와 실제 NUCLEO-F411RE 결과는 구분합니다.

기준: 2026-10-13 · `c5cbb81`. 전체 검증은 [Verification Report](verification.md)에 기록합니다.

## 1. Communication-loss Fail-safe

### 증상과 원인

Linux와 STM32 사이의 통신이 끊기면 Linux가 Fault를 판단해도 OFF 명령을 전달할 수 없습니다. 직전 출력이 ON이면 STM32에 마지막 상태가 남을 수 있어, 출력을 적용하는 MCU에 독립적인 command freshness 감시가 필요했습니다.

### 조치

STM32는 다음 6개 출력 명령이 정확한 순서로 250 ms 이내에 모두 도착해야 complete control refresh로 인정합니다.

```text
PUMP
HEATER
VALVE
BUZZER
RUN_LED
ERROR_LED
```

정상 complete refresh 없이 1000 ms가 경과하면 local safe-state로 Pump / Heater / RUN을 OFF하고 communication-loss latch를 적용합니다. 늦게 도착한 ON과 불완전한 명령 집합으로 재활성화되지 않도록 차단합니다.

Safe complete refresh로 latch를 해제한 뒤 별도 정상 refresh에서 ON을 허용합니다. Timeout 검사는 RX 처리보다 먼저 수행하고 GPIO와 출력 cache를 함께 정리합니다.

Local command-loss RED를 설정한 뒤 초기 SAFE traffic이 바로 표시를 해제하지 못하게 했습니다. Complete refresh와 후속 Linux 운영 상태로 RED 표시 책임을 인계합니다.

### 검증과 결과

실제 보드에서 다음 흐름을 확인했습니다.

```text
Communication loss
STM32 local shutdown
Pump / Heater / RUN OFF
Reconnect
Linux Error State
RESET
```

| 범위 | 판정 |
|---|---|
| Command-loss 물리 출력 차단 | HW VERIFIED |
| Timeout latch / unsafe ON 거부 / safe resync | HOST / HW VERIFIED |
| RED ownership 인계 | HOST VERIFIED |
| 정확한 1000 ms 물리 latency / Local RED 점등 시점 | 별도 HW 계측 범위 |

관련: [STM32 Runtime](../firmware/stm32-equipment-io/stm32-runtime.md), [Hardware Verification](../firmware/stm32-equipment-io/hardware-verification.md).

## 2. DHT11 Decimal Decoder Bug

### 증상과 조사

DHT11 시험의 16회 중 OK는 1회, INVALID_DATA는 15회였습니다. 측정 간격은 2100 ms로 유지됐고 외부 10 kΩ pull-up 비교도 양쪽 `OK 1 / INVALID 15`로 같았습니다.

Diagnostic Harness에서 실제 실패 frame을 확인했습니다.

```text
RAW=2D-02-19-09-51
FRAME=COMPLETE
BITS=40
CHECKSUM_RX=51
CHECKSUM_CALC=51
REASON=HUMIDITY_DECIMAL
```

| Byte | 값 | 의미 |
|---|---|---|
| 0 | 0x2D / 45 | Humidity integer |
| 1 | 0x02 / 2 | Humidity decimal |
| 2 | 0x19 / 25 | Temperature integer |
| 3 | 0x09 / 9 | Temperature decimal |
| 4 | 0x51 | Checksum |

이 frame은 45.2 %RH / 25.9°C이며 길이와 checksum이 정상이었습니다. Decoder가 humidity decimal byte를 0만 허용해 정상 frame을 거부하고 있었습니다.

### 조치와 검증

Decimal byte 0~9를 허용하고 `humidity_tenths = raw[0] * 10 + raw[1]`로 계산했습니다. 실제 문제 frame을 regression vector에 추가했습니다.

수정 당시 CTest **37/37 PASS**, 실제 센서 **16/16 연속 OK**, INVALID_DATA 0, Temperature 25.8°C / Humidity 44.3~44.5 %RH를 확인했습니다.

**Root Cause CONFIRMED / Decoder 수정 완료 / HOST / HW VERIFIED**

관련: [DHT11 Integration](dht11-integration.md), [Frame 테스트](../tests/firmware/test_dht11_frame.c).

## 3. Intermittent UART Telemetry Gap

### 증상

실제 NUCLEO-F411RE + WSL에서 약 1초 동안 정상 sensor telemetry가 수신되지 않는 실행이 있었습니다.

```text
EVENT sensor communication recovered
EVENT Controller state=3 alarm=6
Sensor stale; communication invalid
EVENT UART disconnect
```

마지막 유효 sensor line부터 1000 ms가 경과하면 기존 watchdog이 CommunicationFault / Alarm=6으로 처리합니다.

### 계측과 조사

Flight recorder로 poll / service, readEvents, RX byte count, sensor age, recovery / timeout과 session을 계측했습니다.

Fault 직전 약 1200 ms의 관찰 결과:

| 지표 | 관찰값 |
|---|---|
| readEvents 호출 | 약 112~118회 |
| Median interval | 약 10.1 ms |
| Maximum interval | 약 10.3~10.6 ms |
| Fault 시 sensor age | 약 1000~1003 ms |

해당 관찰 구간에서 Linux Runtime 전체가 약 1초간 멈춘 현상은 나타나지 않았습니다.

일부 실행에는 `PRESSURE:194`, `PRESSURE:1`, `ESTOP:OFDOOR:OPEN` 같은 partial / malformed telemetry가 있었습니다. 별도 실행에서는 Linux RX에 약 1초 동안 byte 자체가 관찰되지 않았습니다.

STM32 USART2 → ST-LINK V2.1 VCP → USB CDC ACM → USB/IP → WSL ttyACM → SerialTransport 경로를 조사했습니다. `dmesg`의 `urb->status -104`, `vhci unlink`는 정상 실행에서도 관찰돼 직접 원인으로 확정하지 않았습니다.

### 판정

Telemetry 공백, 관측 구간의 Linux polling 연속성과 CommunicationFault 재발은 확인했습니다. STM32 TX / ST-LINK VCP / USB-IP 중 실제 단절 구간은 분리하지 못했습니다.

**Symptom HW REPRODUCED / Root Cause UNKNOWN / INVESTIGATION DEFERRED**

후속 조사는 STM32 TX pin과 Linux RX를 동일 시간대에 계측해 실제 송신·수신을 비교합니다.

관련: [UART Diagnostics](uart-diagnostics.md), [UART Recovery](uart-recovery.md).

## 4. Repeated UART Write Stall / Output Coalescing

### 실제 발생 조건

하나의 sensor RX batch에 여러 line이 들어오면 기존 Runtime은 각 line의 상태 반영 과정에서 `sendOutputs()`를 반복 호출했습니다. 출력이 같거나 거의 같아도 같은 service에서 갱신을 여러 번 송신할 수 있었습니다.

평소 write는 약 0.1 ms, service는 약 1 ms 수준이었습니다. 실제 계측에서 write 한 번이 약 110 ms로 지연되는 구간이 있었고 같은 service의 5~6회 반복 송신이 지연을 누적했습니다.

| 조건 | 누적 처리 예 |
|---|---|
| write 약 110 ms · 5회 | service 약 550 ms |
| write 약 110 ms · 6회 | service 약 660 ms |

긴 write와 동일 batch 내부의 반복 output refresh가 결합해 service 처리시간이 커졌습니다. 수정 대상으로 특정한 원인은 sensor line별로 반복하던 출력 갱신 구조입니다.

### 조치

[96cac56 — perf: coalesce UART output updates](https://github.com/triton0305/equipment-controller/commit/96cac56ecaff7c727dc7a41ddd333069a2f4c595)에서 일반 Sensor-driven output refresh를 batch 단위로 모았습니다.

Sensor 값을 반영한 뒤 필요한 출력 묶음을 송신합니다. 새로운 정상 batch에서는 출력이 같아도 MCU command freshness를 refresh합니다. Safety Fault는 다음 RX event 전에 안전 출력을 송신하고 실제 제어 명령의 송신 경로도 유지합니다.

### 검증과 결과

Host의 batch / delay / timestamp / safety / commands 시나리오에서 송신 횟수, 110 ms 지연 조건, 수신시각 유지, Fault 우선 출력과 명령 처리를 검증했습니다. 전체 regression은 현재 53/53 PASS 기준선에 포함됩니다.

**Repeated Output Refresh RESOLVED / Coalescing IMPLEMENTED / HOST VERIFIED**

하나의 RX batch에서 반복 write에 의해 발생할 수 있는 누적 service latency를 줄였습니다. 수정 후 물리 latency 감소량은 [Verification Report](verification.md)의 정량 HW 항목으로 관리합니다.

## 5. Linux Serial Disconnect / Reconnect Handling

### 증상과 원인

PTY peer를 닫은 후에도 `read_ok=1 isOpen=1`이 유지됐습니다. 기존 Transport가 nonblocking TTY의 `read()==0`을 정상 idle처럼 처리하고 있어 반환값만으로 단절을 구분하기 어려웠습니다.

### 조치

`poll()`에서 `POLLHUP / POLLERR / POLLNVAL`을 확인하면 read failure를 전달하고 `closePort()`로 포트를 닫도록 수정했습니다. FD, RX buffer와 oversized discard 상태를 함께 초기화했습니다.

### 검증

단절 후 `read_ok=0 isOpen=0`을 확인했습니다. 같은 SerialTransport 객체를 새 PTY endpoint에 연결한 뒤 `TEMP:31.5`를 수신했습니다.

**Disconnect 감지 / Port 상태 초기화 / 동일 객체 재연결: HOST VERIFIED**

이 결과는 Linux PTY Transport 범위로 기록합니다. 실제 전체 UART 통신 경로의 장시간 안정성과 구분합니다.

관련: [Transport](../modules/transport/src/serial_transport.cpp), [Transport 테스트](../tests/transport/test_serial_transport.cpp).

## Summary

| 사례 | 원인 판단 | 조치 / 상태 | 검증 |
|---|---|---|---|
| Communication-loss Fail-safe | 독립 감시 부재 확인 | Local shutdown · latch · safe resync | 출력 HW / RED Host |
| DHT11 Decoder | Validation 조건 확인 | Decimal 처리 수정 | HOST / HW |
| UART telemetry 공백 | 발생 계층 미확정 | 동시 계측 후속 과제 | HW REPRODUCED |
| 반복 output / write 누적 | 반복 갱신 구조 확인 | Batch coalescing | HOST |
| Serial disconnect / reconnect | 감지 경로 확인 | poll event · 상태 초기화 | HOST |

실제 출력 안전정지·DHT11 경로와 주요 Interlock 검증을 유지합니다. 간헐 telemetry 공백은 원인 미확정 상태로 보존하며 STM32 TX / Linux RX의 동시 계측을 우선합니다.
