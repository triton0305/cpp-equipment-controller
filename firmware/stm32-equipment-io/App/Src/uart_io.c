#include "uart_io.h"

static UART_HandleTypeDef *handle;
void UartIo_Init(UART_HandleTypeDef *uart) { handle = uart; }

int UartIo_TryRead(uint8_t *byte)
{
  if (!handle) return 0;
  uint32_t status = handle->Instance->SR;
  if (status & (USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE))
  {
    /* F4 error clearing sequence: SR read followed by DR read. */
    volatile uint32_t discard = handle->Instance->DR;
    (void)discard;
    return -1;
  }
  if (!(status & USART_SR_RXNE)) return 0;
  *byte = (uint8_t)handle->Instance->DR;
  return 1;
}

bool UartIo_TryWrite(uint8_t byte)
{
  if (!handle || !(handle->Instance->SR & USART_SR_TXE)) return false;
  handle->Instance->DR = byte;
  return true;
}
