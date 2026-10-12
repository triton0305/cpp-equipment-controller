#ifndef TEST_DHT11_HAL_H
#define TEST_DHT11_HAL_H
#include "../fakes/stm32f4xx_hal.h"

typedef struct { unsigned unused; } TIM_TypeDef;
extern TIM_TypeDef fake_tim5;
#define TIM5 (&fake_tim5)
#define TIM_COUNTERMODE_UP 0U
typedef struct { uint32_t Prescaler, Period, CounterMode; } TIM_Base_InitTypeDef;
typedef struct { TIM_TypeDef *Instance; TIM_Base_InitTypeDef Init; } TIM_HandleTypeDef;
uint32_t fake_dht11_counter(void);
#define __HAL_TIM_GET_COUNTER(h) ((void)(h), fake_dht11_counter())
HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *timer);
typedef struct { uint32_t Pin, Mode, Pull, Speed; } GPIO_InitTypeDef;
#define GPIO_MODE_INPUT 0U
#define GPIO_MODE_OUTPUT_PP 1U
#define GPIO_NOPULL 0U
#define GPIO_SPEED_FREQ_LOW 0U
void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *gpio);
#endif
