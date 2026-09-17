/*
 * uart.h
 *
 * Created: 3.1.2026 9.44.09
 *  Author: Mikael Niemelä
 */ 

#ifndef UART_H
#define UART_H

void uart_init(uint8_t highSpeedMode);
void startTx();
void addTxData(char);
uint8_t getFreeBuffer();
uint8_t getUsedBuffer();
void setCallback(void (*callback_)(uint8_t, int8_t, int8_t, uint8_t));

#endif