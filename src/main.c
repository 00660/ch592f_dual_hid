#include "CONFIG.h"
#include "HAL.h"
#include "ble_app.h"
#include "serial_ota.h"
#include "diag.h"
#include "led.h"

__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];

__HIGH_CODE
__attribute__((noinline))
static void run(void)
{
    for (;;) {
        TMOS_SystemProcess();
        serial_poll(&app_ota);
    }
}

int main(void)
{
    uint8_t status;
    led_init();
    diag_entry();
    SetSysClock(CLK_SOURCE_PLL_60MHz);
    diag_uart_init();
    diag_printf("[APP] clock=%u\n",(unsigned)GetSysClock());
    serial_init();
    diag_printf("[APP] BLE begin heap=%u buffers=%u links=%u\n",
                (unsigned)BLE_MEMHEAP_SIZE,(unsigned)BLE_BUFF_NUM,(unsigned)PERIPHERAL_MAX_CONNECTION);
    CH59x_BLEInit();
    diag_printf("[APP] BLE OK; HAL begin\n");
    HAL_Init();
    diag_printf("[APP] HAL OK; peripheral begin\n");
    status = GAPRole_PeripheralInit();
    diag_printf("[APP] peripheral rc=%02X\n",(unsigned)status);
    if (status != SUCCESS) for (;;) {}
    ble_app_init();
    diag_printf("[APP] scheduler enter\n");
    run();
}
