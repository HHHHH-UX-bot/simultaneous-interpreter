#ifndef NETWORK_UI_WIFI_RECOVER_H
#define NETWORK_UI_WIFI_RECOVER_H
#include "../ps_wifi/wifi_at_probe.h"
#ifdef Z7_UART_1M_STREAM
#define WIFI_STREAM_DIVISOR 50u
#define WIFI_STREAM_BAUD 1000000u
#define WIFI_STREAM_UART_COMMAND "AT+UART_CUR=1000000,8,1,0,0\r\n"
#elif defined(Z7_UART_460800_STREAM)
#define WIFI_STREAM_DIVISOR 109u
#define WIFI_STREAM_BAUD 460800u
#define WIFI_STREAM_UART_COMMAND "AT+UART_CUR=460800,8,1,0,0\r\n"
#elif defined(Z7_UART_LEGACY_STREAM)
#define WIFI_STREAM_DIVISOR 54u
#define WIFI_STREAM_BAUD 921600u
#define WIFI_STREAM_UART_COMMAND "AT+UART_CUR=921600,8,1,0,0\r\n"
#else
#define WIFI_STREAM_DIVISOR 25u
#define WIFI_STREAM_BAUD 2000000u
#define WIFI_STREAM_UART_COMMAND "AT+UART_CUR=2000000,8,1,0,0\r\n"
#endif
/* Recover a failed session only. No credentials or Flash configuration. */
void wifi_recover_begin(wifi_read_fn read,wifi_write_fn write,uint32_t now);
/* 0: in progress, 1: verified high-speed UART, -1: bounded failure. */
int wifi_recover_tick(uint32_t now);
uint32_t wifi_recover_phase(void);
#endif
