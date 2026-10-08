#include "uart_io.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
int main(void)
{
  uint8_t byte = 42;
  UartIo_Init(NULL);
  CHECK(UartIo_TryRead(&byte) == 0 && !UartIo_TryWrite('x'));
  USART_TypeDef registers = {0}; UART_HandleTypeDef uart = {&registers};
  UartIo_Init(&uart);
  CHECK(UartIo_TryRead(&byte) == 0 && byte == 42);
  CHECK(!UartIo_TryWrite('x'));
  registers.SR = USART_SR_RXNE; registers.DR = 'A';
  CHECK(UartIo_TryRead(&byte) == 1 && byte == 'A');
  const uint32_t errors[] = {USART_SR_PE, USART_SR_FE, USART_SR_NE, USART_SR_ORE};
  for (unsigned i = 0; i < 4; ++i)
  {
    registers.SR = USART_SR_RXNE | errors[i];
    byte = 42;
    CHECK(UartIo_TryRead(&byte) == -1 && byte == 42);
  }
  registers.SR = USART_SR_TXE;
  CHECK(UartIo_TryWrite('Z') && registers.DR == 'Z');
  /* Plain fake registers cannot prove peripheral SR/DR side effects or timing. */
  return 0;
}
