#include "layout.h"
#include "diag.h"

int main(void)
{
    diag_entry();
    diag_printf("[JUMP] target=%08X word=%08X\n",(unsigned)IAP_BASE,
                (unsigned)*(volatile uint32_t *)IAP_BASE);
    diag_flush();
    __asm__ volatile ("csrci mstatus, 8");
    ((void (*)(void))IAP_BASE)();
    for (;;) {}
}
