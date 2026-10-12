#ifndef DHT11_H
#define DHT11_H

#include "dht11_frame.h"
#include "stm32f4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DHT11_STABILIZE_MS 2100U
#define DHT11_INTERVAL_MS 2100U
#define DHT11_START_LOW_MS 20U
#define DHT11_CAPTURE_LIMIT_US 6000U

/* Single PC4 sensor, foreground only, not reentrant. Call once after MX_TIM5_Init
 * and sensor power-on. TIM5 must remain free-running at 1 MHz, ARR UINT32_MAX.
 * No timer reset/stop or IRQ masking. External DATA pull-up is required. */
Dht11Result Dht11_Init(TIM_HandleTypeDef *timer);
/* BUSY means the start was accepted OR an existing start is still pending.
 * NOT_READY means stabilization/cooldown. Failed attempts also consume cooldown. */
Dht11Result Dht11_Request(void);
/* Call frequently after Request: LOW is held cooperatively for >=20 ms.
 * Service before 30 ms; late service releases PC4 and returns DATA_TIMEOUT.
 * At capture this blocks for <=6000 timer us + software/interrupt overhead.
 * Output is written only on OK; idle Poll returns NOT_READY (no cached result). */
Dht11Result Dht11_Poll(Dht11Sample *sample);
/* Release a pending start; preserve the cooldown. */
void Dht11_Cancel(void);

#ifdef __cplusplus
}
#endif
#endif
