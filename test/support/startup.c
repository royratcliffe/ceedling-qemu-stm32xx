#include "stm32f4xx_ll_usart.h"

#include "cmsis_compiler.h"

extern void main(void);

int putchar(int ch) {
  LL_USART_TransmitData8(USART1, ch);
  return 0;
}

void Reset_Handler() {
  main();
  __NVIC_SystemReset();
}
