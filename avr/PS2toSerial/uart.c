/*
 * uart.c
 *
 * Created: 3.1.2026 9.44.32
 *  Author: Mikael Niemelä
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>

#include "uart.h"
#include "ringbuf.h"
#include "constants.h"

char txBuf_array[TX_BUF_SIZE];
struct ringBuffer txBuf;

void (*callback)(uint8_t, int8_t, int8_t, uint8_t);

void startTx() {
	UCSRB |= 1 << UDRIE;
}

void uart_init(uint8_t highSpeedMode) {
	if (highSpeedMode) {
		#define UBRR_HIGH_VALUE (uint16_t)((F_CPU) / (8.0 * 9600) - 0.5)
		UBRRH = (uint8_t)((0xFF00 & UBRR_HIGH_VALUE) >> 8); 
		UBRRL = (uint8_t)(0xFF & UBRR_HIGH_VALUE);
	} else {
		#define UBRR_LOW_VALUE (uint16_t)((F_CPU) / (8.0 * 1200) - 0.5)
		UBRRH = (uint8_t)((0xFF00 & UBRR_LOW_VALUE) >> 8);
		UBRRL = (uint8_t)(0xFF & UBRR_LOW_VALUE);
	}
	UCSRA = (1<<U2X);
	UCSRC = (1<<UCSZ1);
	UCSRB = (1<<TXEN);
	
	init_buf(&txBuf, TX_BUF_SIZE, txBuf_array);
}

void addTxData(char data) {
	add(&txBuf, data);
}

uint8_t getFreeBuffer() {
	return TX_BUF_SIZE - elements(&txBuf);
}

uint8_t getUsedBuffer() {
	return elements(&txBuf);
}

void setCallback(void (*callback_)(uint8_t, int8_t, int8_t, uint8_t)) {
	callback = callback_;
	if (callback != 0)
		UCSRB |= 1 << TXCIE;
}

ISR(USART_TX_vect) {
	UCSRB &= ~(1 << TXCIE);
	if (callback != 0) {
		callback(0,0,0,1);
	}
}

ISR(USART_UDRE_vect) {
	if (elements(&txBuf) > 0)
		UDR = remove(&txBuf);
	//TX complete
	else {
		UCSRB &= ~(1 << UDRIE);
		// if combining packets, send last data
	}
}
