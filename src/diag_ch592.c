#include "CH59x_common.h"
#include "diag.h"
#include "layout.h"

#ifndef DIAG_STAGE
#error DIAG_STAGE must identify the linked image.
#endif

static uint8_t quiet;

void diag_set_quiet(uint8_t value) { quiet = value; }
uint8_t diag_is_quiet(void) { return quiet; }

void diag_uart_init(void)
{
    PFIC_DisableIRQ(UART1_IRQn);
    R16_PIN_ALTERNATE &= ~RB_PIN_UART1;
    GPIOA_SetBits(bTXD1);
    GPIOA_ModeCfg(bTXD1,GPIO_ModeOut_PP_5mA);
    GPIOA_ModeCfg(bRXD1,GPIO_ModeIN_PU);
    UART1_DefInit();
    quiet = 0;
}

void diag_putc(char value)
{
    uint32_t budget = 100000;
    while (R8_UART1_TFC >= UART_FIFO_SIZE && --budget) {}
    if (budget) R8_UART1_THR = (uint8_t)value;
}

void diag_flush(void)
{
    uint32_t budget = 1000000;
    while (!(R8_UART1_LSR & RB_LSR_TX_ALL_EMP) && --budget) {}
}

void diag_entry(void)
{
    uint32_t status;
    diag_uart_init();
    diag_printf("[%s] entry fw=%08X reset=%02X chip=%02X\n",
                DIAG_STAGE,(unsigned)FW_VERSION,(unsigned)SYS_GetLastResetSta(),(unsigned)R8_CHIP_ID);
    __asm__ volatile ("csrr %0, mstatus" : "=r"(status));
    diag_printf("[%s] mstatus=%08X clock=%u\n",DIAG_STAGE,(unsigned)status,(unsigned)GetSysClock());
    diag_flush();
}

void diag_fault(uint32_t cause, uint32_t pc, uint32_t value, uint32_t stack)
{
    diag_uart_init();
    diag_printf("\n[FAULT %s] cause=%08X pc=%08X value=%08X sp=%08X\n",
                DIAG_STAGE,(unsigned)cause,(unsigned)pc,(unsigned)value,(unsigned)stack);
    diag_flush();
    for (;;) {}
}
