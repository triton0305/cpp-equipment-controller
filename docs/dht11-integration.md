# DHT11 Integration

DHT11 Temperature / Humidity를 STM32에서 수집하고 UART를 거쳐 Linux Runtime과 CLI snapshot에 반영합니다. 기준은 2026-10-13 · `c5cbb81`입니다.

## 데이터 경로

| 구간 | 처리 |
|---|---|
| PC4 / TIM5 | DHT11 시작 신호 · pulse capture |
| `Dht11_Poll` | 40-bit frame · checksum · decimal / range 검사 |
| `IoEnvironment` | 새 측정 결과와 유효성 관리 |
| `IoProtocol` | TEMP / HUMIDITY ASCII 직렬화 |
| Linux Parser | SensorState 값·validity 갱신 |
| ControllerRuntime | freshness · Critical 관측 · STATUS / CLI snapshot |

Production main은 TIM5 초기화 후 `IoEnvironment_Init(&htim5)`를 호출합니다. 독립 Hardware Harness와 운영 Runtime 경로를 구분합니다.

온습도는 새 측정 결과가 있을 때만 기존 Door / E-Stop / Pressure 보고 뒤에 전송합니다. 온습도는 0.1 단위로 직렬화하며 Linux 상태 표시는 소수점 한 자리입니다.

실제 Production UART에서 확인한 값:

```text
TEMP:21.3
HUMIDITY:46.2
```

## Decoder

실제 frame `2D-02-19-09-51`은 45.2 %RH / 25.9°C이고 checksum은 0x51입니다. Humidity decimal byte 0~9를 허용하며 `raw[0] * 10 + raw[1]`로 0.1 단위 값을 계산합니다.

기존 decoder의 checksum과 최종 범위 검사(-20~60°C, 5~95%RH)를 유지합니다. Linux Humidity parser는 0~100% 범위를 검사하고 NaN / Inf, 빈 값과 숫자 뒤 잡문자를 거부합니다. 분석 과정은 [Troubleshooting](troubleshooting.md)에 기록합니다.

## 측정과 Freshness

- 전원 인가 후 안정화와 최소 측정 간격은 2.1초입니다.
- START LOW 20 ms는 cooperative 대기이고 pulse capture는 최대 6 ms 예산을 사용합니다.
- 성공값은 새 측정 한 번당 한 번 전송합니다. Cached 숫자를 반복 전송해 freshness를 연장하지 않습니다.
- 응답 / data timeout, checksum / range 오류는 `TEMP:INVALID`, `HUMIDITY:INVALID`로 전달합니다.
- STM32와 Linux에서 마지막 정상 온습도는 5초 기준으로 만료합니다.
- Linux는 Temperature와 Humidity의 수신 시각을 각각 관리합니다.
- UART fault / disconnect는 validity를 폐기하며 새 정상 측정으로 복구합니다.
- INVALID / malformed 측정은 통신 liveness deadline을 갱신하지 않습니다.
- 새 측정 전과 invalid / stale 상태는 `unavailable`로 표시합니다.

TX backpressure에서 오래된 값이나 이후 실패로 무효화된 미완료 packet은 폐기합니다. 전송 중 partial 숫자는 `!\n`으로 무효화한 뒤 INVALID line을 보냅니다.

## UART와 Safety

Capture 중 foreground hook이 UART byte를 128-byte queue에 보관합니다. Hook에서는 명령 parsing이나 blocking TX를 수행하지 않습니다. Overflow / hardware RX 오류는 queue를 폐기하고 line discard 경로로 전달합니다.

Command timeout은 capture 전후와 RX parsing 전에 검사합니다. Queue에 늦게 남은 ON 명령이 fail-safe latch를 우회하지 않도록 Host regression으로 확인합니다.

Temperature는 READY 시작 자격 범위와 Critical Safety에 사용하고 Humidity는 Critical Safety에 사용합니다. 현재 시간 기반 공정은 [Process Control](process-control.md)의 정책을 따릅니다.

## 검증

| 항목 | 결과 |
|---|---|
| 실제 문제 frame 회귀 테스트 | PASS |
| Decoder 수정 당시 전체 CTest | 37/37 PASS |
| 독립 DHT11 Hardware Harness | 16/16 연속 OK · INVALID_DATA 0 |
| Production 통합 당시 전체 CTest | 44/44 PASS |
| 현재 전체 Host 기준선 | 53/53 PASS |
| 실제 UART TEMP / HUMIDITY 수신 | HW VERIFIED |
| Linux STATUS / CLI의 실제값 표시 | HW VERIFIED |

Production 전체 WCET, DHT capture의 물리 Safety latency, 오류 종류별 실제 센서 시험과 정밀 timing은 별도 계측 범위입니다. 정상 범위 Temperature에서 READY를 확인했으며 시작 범위 밖의 HW 시험은 [Verification Report](verification.md)에서 관리합니다.

관련: [STM32 Runtime](../firmware/stm32-equipment-io/stm32-runtime.md), [Controller Runtime](controller-runtime.md).
