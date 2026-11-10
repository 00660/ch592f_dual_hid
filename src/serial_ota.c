#include "CH59x_common.h"
#include "serial_ota.h"
#include "wire.h"
#include "diag.h"

static wire_t wire;
static uint16_t idle_ticks;
static uint16_t transaction_idle;
#ifndef DUAL_IAP
#define RX_CAPACITY 512u
static volatile uint16_t rx_head, rx_tail;
static volatile uint8_t rx_overflow;
static volatile uint8_t rx_bytes[RX_CAPACITY];

__INTERRUPT
__HIGH_CODE
void UART1_IRQHandler(void)
{
    uint8_t interrupt = UART1_GetITFlag();
    if (interrupt == UART_II_LINE_STAT) {
        uint8_t status = UART1_GetLinSTA();
        if (status & (RB_LSR_OVER_ERR | RB_LSR_PAR_ERR | RB_LSR_FRAME_ERR))
            rx_overflow = 1;
    }
    while (R8_UART1_RFC) {
        uint8_t byte = UART1_RecvByte();
        uint16_t next = (rx_head + 1) % RX_CAPACITY;
        if (next == rx_tail) rx_overflow = 1;
        else {
            rx_bytes[rx_head] = byte;
            rx_head = next;
        }
    }
}
#endif

void serial_init(void)
{
    diag_flush();
    diag_uart_init();
#ifndef DUAL_IAP
    UART1_ByteTrigCfg(UART_1BYTE_TRIG);
    UART1_INTCfg(ENABLE, RB_IER_RECV_RDY | RB_IER_LINE_STAT);
    PFIC_EnableIRQ(UART1_IRQn);
#endif
    wire_reset(&wire);
}

static int receive_byte(void)
{
#ifdef DUAL_IAP
    return R8_UART1_RFC ? UART1_RecvByte() : -1;
#else
    int byte;
    if (rx_overflow) {
        PFIC_DisableIRQ(UART1_IRQn);
        rx_tail = rx_head;
        rx_overflow = 0;
        PFIC_EnableIRQ(UART1_IRQn);
        wire_reset(&wire);
        return -1;
    }
    if (rx_tail == rx_head) return -1;
    byte = rx_bytes[rx_tail];
    rx_tail = (rx_tail + 1) % RX_CAPACITY;
    return byte;
#endif
}

void serial_tick(uint16_t elapsed_ms)
{
    if (transaction_idle < 30000) transaction_idle += elapsed_ms;
    idle_ticks += elapsed_ms;
    if (idle_ticks >= 100) {
        wire_reset(&wire);
        idle_ticks = 100;
    }
}

void serial_poll(ota_t *ota)
{
    int byte, size;
    uint8_t response[OTA_STATUS_SIZE], frame[OTA_STATUS_SIZE + 8];
    if (transaction_idle >= 30000) ota_disconnect(ota,OTA_OWNER_UART);
    while ((byte = receive_byte()) >= 0) {
        idle_ticks = 0;
        size = wire_feed(&wire, (uint8_t)byte);
        if (size > 0) {
            /* Binary protocol owns TX until reset; no text between OTA replies. */
            diag_set_quiet(1);
            transaction_idle = 0;
            uint8_t status = ota_command(ota, OTA_OWNER_UART, wire.bytes + 4, size);
            uint16_t frame_size;
            ota_status(ota, response);
            response[1] = wire.bytes[4];
            if (wire.bytes[4] != OTA_INFO || status != RESULT_OK)
                response[2] = status;
            frame_size = wire_encode(frame, response, sizeof(response));
            UART1_SendString(frame, frame_size);
            return;
        }
    }
}
