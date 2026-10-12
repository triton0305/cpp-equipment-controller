#include "uart_io.h"

static UART_HandleTypeDef *handle;
/* 128 bytes exceed the 70 bytes possible during a 6 ms capture at 115200 baud.
   Both producers/consumers run in foreground; no IRQ/peripheral changes. */
static uint8_t captured[128];
static unsigned head, count;
static bool capture_error;
void UartIo_Init(UART_HandleTypeDef *uart)
{
  handle = uart;
  head = count = 0;
  capture_error = false;
}

static int read_hardware(uint8_t *byte)
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

void UartIo_BufferRx(void)
{
  uint8_t byte;
  const int result = read_hardware(&byte);
  if (result < 0 || (result > 0 && count == sizeof(captured)))
  {
    capture_error = true;
    head = count = 0;
  }
  else if (result > 0 && !capture_error)
  {
    captured[(head + count) % sizeof(captured)] = byte;
    ++count;
  }
}

int UartIo_TryRead(uint8_t *byte)
{
  if (capture_error)
  {
    capture_error = false;
    return -1; // Runtime discards the fragmented command before resuming RX.
  }
  if (count)
  {
    *byte = captured[head];
    head = (head + 1U) % sizeof(captured);
    --count;
    return 1;
  }
  return read_hardware(byte);
}

bool UartIo_TryWrite(uint8_t byte)
{
  if (!handle || !(handle->Instance->SR & USART_SR_TXE)) return false;
  handle->Instance->DR = byte;
  return true;
}
