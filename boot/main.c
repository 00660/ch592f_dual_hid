#include "CH59x_common.h"
#include "ota.h"
#include "storage.h"
#include "serial_ota.h"
#include "diag.h"
#include "led.h"

#ifndef FACTORY_SIZE
#error Factory image metadata must be supplied by the build.
#endif

static ota_t ota;

int main(void)
{
    image_meta_t factory, pending;
    uint16_t ticks = 0;
    uint16_t ready_ticks = 0;
    uint32_t tick_start;
    int valid, installed;
    led_init();
    diag_entry();
    SetSysClock(CLK_SOURCE_PLL_60MHz);
    diag_uart_init();
    diag_printf("[IAP] clock=%u; pending check\n",(unsigned)GetSysClock());
    storage_meta_read(PENDING_META,&pending);
    diag_printf("[IAP] pending valid=%u size=%u crc=%08X\n",
                (unsigned)meta_valid(&pending),(unsigned)pending.size,(unsigned)pending.crc);
    installed = boot_install_pending();
    diag_printf("[IAP] install result=%d; active check\n",installed);
    valid = boot_active_valid();
    diag_printf("[IAP] active valid=%u\n",(unsigned)valid);
    if (!valid) {
        uint32_t actual = storage_crc(APP_BASE,FACTORY_SIZE);
        diag_printf("[IAP] factory size=%u expected=%08X actual=%08X\n",
                    (unsigned)FACTORY_SIZE,(unsigned)FACTORY_CRC,(unsigned)actual);
        if (actual == FACTORY_CRC) {
            int status;
            meta_make(&factory,FACTORY_SIZE,FACTORY_CRC,FW_VERSION);
            status = storage_meta_write(ACTIVE_META,&factory);
            diag_printf("[IAP] active metadata rc=%d\n",status);
            valid = status == 0;
        }
    }
    diag_flush();
    serial_init();
    diag_printf(valid ? "[IAP] UART recovery window 500ms\n" : "[IAP] APP invalid; UART recovery waiting\n");
    diag_flush();
    ota_init(&ota);
    SysTick_Config(SysTick_LOAD_RELOAD_Msk);
    PFIC_DisableIRQ(SysTick_IRQn);
    tick_start = SYS_GetSysTickCnt();
    for (;;) {
        serial_poll(&ota);
        if ((uint32_t)(SYS_GetSysTickCnt() - tick_start) < 60000u) continue;
        tick_start = SYS_GetSysTickCnt();
        if (ota.state == OTA_READY && ++ready_ticks >= 1500) SYS_ResetExecute();
        /* Keep a recovery window even when the application passes CRC. */
        if (ticks < 500) ++ticks;
        if (valid && ticks >= 500 && ota.state == OTA_IDLE) {
            diag_printf("[IAP] jump APP=%08X\n",(unsigned)APP_BASE);
            diag_flush();
            UART1_Reset();
            SysTick->CTLR = 0;
            PFIC_ClearPendingIRQ(SysTick_IRQn);
            __asm__ volatile ("csrci mstatus, 8");
            ((void (*)(void))APP_BASE)();
        }
        ota_poll(&ota);
        serial_tick(1);
    }
}
