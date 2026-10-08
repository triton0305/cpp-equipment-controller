#ifndef TEST_HAL_H
#define TEST_HAL_H
#include <stdint.h>
#include <stdbool.h>
typedef enum { HAL_OK, HAL_ERROR } HAL_StatusTypeDef;
typedef struct { unsigned unused; } ADC_HandleTypeDef;
#define ADC_FLAG_EOC 1U
extern bool fake_adc_eoc;
#define __HAL_ADC_GET_FLAG(h, f) ((void)(h), (void)(f), fake_adc_eoc)
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *h);
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *h);
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *h);

typedef struct { uint16_t input, output; } GPIO_TypeDef;
extern GPIO_TypeDef fake_gpio[3];
#define GPIOA (&fake_gpio[0])
#define GPIOB (&fake_gpio[1])
#define GPIOC (&fake_gpio[2])
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
typedef struct { volatile uint32_t SR, DR; } USART_TypeDef;
typedef struct { USART_TypeDef *Instance; } UART_HandleTypeDef;
uint32_t HAL_GetTick(void);
#define USART_SR_PE (1U << 0)
#define USART_SR_FE (1U << 1)
#define USART_SR_NE (1U << 2)
#define USART_SR_ORE (1U << 3)
#define USART_SR_RXNE (1U << 5)
#define USART_SR_TXE (1U << 7)
#define GPIO_PIN_0 (1U << 0)
#define GPIO_PIN_1 (1U << 1)
#define GPIO_PIN_2 (1U << 2)
#define GPIO_PIN_3 (1U << 3)
#define GPIO_PIN_4 (1U << 4)
#define GPIO_PIN_5 (1U << 5)
#define GPIO_PIN_6 (1U << 6)
#define GPIO_PIN_7 (1U << 7)
#define GPIO_PIN_8 (1U << 8)
#define GPIO_PIN_9 (1U << 9)
#define GPIO_PIN_10 (1U << 10)
#define GPIO_PIN_11 (1U << 11)
#define GPIO_PIN_12 (1U << 12)
#define GPIO_PIN_13 (1U << 13)
#define GPIO_PIN_14 (1U << 14)
#define GPIO_PIN_15 (1U << 15)
#endif
