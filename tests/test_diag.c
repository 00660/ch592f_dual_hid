#include "diag.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static char captured[512];
static unsigned used;
static uint8_t quiet;

uint8_t diag_is_quiet(void) { return quiet; }
void diag_putc(char value)
{
    assert(used+1 < sizeof(captured));
    captured[used++] = value;
    captured[used] = 0;
}

int main(void)
{
    diag_printf("[APP] pc=%08X reset=%02X clock=%u rc=%d %s %%\n",
                0x1000u,1u,60000000u,-1,"ready");
    assert(!strcmp(captured,"[APP] pc=00001000 reset=01 clock=60000000 rc=-1 ready %\r\n"));
    used = 0;
    diag_printf("%d %u %x %c",INT_MIN,UINT_MAX,0xDEADBEEFu,'!');
    assert(!strcmp(captured,"-2147483648 4294967295 DEADBEEF !"));
    quiet = 1;
    used = 0;
    assert(diag_printf("must not contaminate UART OTA\n") == 0);
    assert(used == 0);
    puts("PASS diagnostic formatting, signed bounds, CRLF and UART quiet mode");
    return 0;
}
