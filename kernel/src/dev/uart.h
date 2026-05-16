#ifndef DEV_UART_H
#define DEV_UART_H

#include <stddef.h>
#include <stdint.h>

// credits: https://github.com/kevinalavik/lyr/blob/main/kernel/include/dev/uart.h

#define DEFAULT_UART_PORT 0x3F8
#define DEFAULT_UART_BAUD_RATE 115200

#define UART_REG_RBR 0
#define UART_REG_THR 0
#define UART_REG_IER 1
#define UART_REG_DLL 0
#define UART_REG_DLM 1
#define UART_REG_IIR 2
#define UART_REG_FCR 2
#define UART_REG_LCR 3
#define UART_REG_MCR 4
#define UART_REG_LSR 5
#define UART_REG_MSR 6
#define UART_REG_SCR 7

typedef struct {
	uint8_t db : 2;
	uint8_t sb : 1;
	uint8_t pb : 3;
	uint8_t be : 1;
	uint8_t dlab : 1;
} __attribute__((packed)) uart_lcr_t;

#define UART_DATA_BITS_5 0
#define UART_DATA_BITS_6 1
#define UART_DATA_BITS_7 2
#define UART_DATA_BITS_8 3

#define UART_STOP_BITS_1 0
#define UART_STOP_BITS_2 1

#define UART_PARITY_NONE 0
#define UART_PARITY_ODD 1
#define UART_PARITY_EVEN 2
#define UART_PARITY_MARK 3
#define UART_PARITY_SPACE 4

typedef struct {
	uint8_t received_data_available : 1;
	uint8_t transmitter_holding_register_empty : 1;
	uint8_t receiver_line_status : 1;
	uint8_t modem_status : 1;
	uint8_t reserved : 4;
} __attribute__((packed)) uart_ier_t;

typedef struct {
	uint8_t enable_fifo : 1;
	uint8_t clear_receive_fifo : 1;
	uint8_t clear_transmit_fifo : 1;
	uint8_t dma_mode_select : 1;
	uint8_t reserved : 4;
} __attribute__((packed)) uart_fcr_t;

#define UART_FCR_TRIGGER_LEVEL_1 0
#define UART_FCR_TRIGGER_LEVEL_4 0x40
#define UART_FCR_TRIGGER_LEVEL_8 0x80
#define UART_FCR_TRIGGER_LEVEL_14 0xC0

typedef struct {
	uint8_t interrupt_pending : 1;
	uint8_t interrupt_state : 2;
	uint8_t timeout_interrupt_pending : 1;
	uint8_t reserved : 4;
} __attribute__((packed)) uart_iir_t;

#define UART_IIR_INTERRUPT_MODEM_STATUS 0
#define UART_IIR_INTERRUPT_TRANSMITTER_HOLDING_REGISTER_EMPTY 1
#define UART_IIR_INTERRUPT_RECEIVED_DATA_AVAILABLE 2
#define UART_IIR_INTERRUPT_RECEIVER_LINE_STATUS 3

typedef struct {
	uint8_t fifo_enabled : 1;
	uint8_t fifo_usable : 1;
	uint8_t reserved : 6;
} __attribute__((packed)) uart_fifo_state_t;

typedef struct {
	uint8_t dtr : 1;
	uint8_t rts : 1;
	uint8_t out1 : 1;
	uint8_t out2 : 1;
	uint8_t loop : 1;
	uint8_t reserved : 3;
} __attribute__((packed)) uart_mcr_t;

typedef struct {
	uint8_t data_ready : 1;
	uint8_t overrun_error : 1;
	uint8_t parity_error : 1;
	uint8_t framing_error : 1;
	uint8_t break_indicator : 1;
	uint8_t transmitter_holding_register_empty : 1;
	uint8_t transmitter_empty : 1;
	uint8_t impending_error : 1;
} __attribute__((packed)) uart_lsr_t;

typedef struct {
	uint8_t delta_clear_to_send : 1;
	uint8_t delta_data_set_ready : 1;
	uint8_t trailing_edge_of_ring_indicator : 1;
	uint8_t delta_data_carrier_detect : 1;
	uint8_t clear_to_send : 1;
	uint8_t data_set_ready : 1;
	uint8_t ring_indicator : 1;
	uint8_t data_carrier_detect : 1;
} __attribute__((packed)) uart_msr_t;

int uart_init();
void uart_wbuf(const char *buf, size_t len);
void uart_wstr(const char *str);
void uart_wch(char c);

#endif // DEV_UART_H