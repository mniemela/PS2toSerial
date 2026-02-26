/*
 * ps2.c
 *
 * Created: 4.1.2026 14.05.25
 *  Author: Mikael Niemelä
 */ 


#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/parity.h>

#include "ps2.h"
#include "uart.h"
#include "constants.h"
#include <util/delay.h>

#include "ringbuf.h"

char ps2TxBuf_array[PS2_TX_BUF_SIZE];
char rxArray[4];

struct ringBuffer ps2TxBuf;

static void (*handleOverflow)();
static void (*handleRxByte)(char);

static void handleRx();
static void handleResponse(char);

static char mouseType = 0;
static uint8_t firstByteReceived = 0;
static char parityAndStopBits;
static char previousSentByte;
static uint8_t bytesReceived = 0;
static uint8_t sampleRate;

static char reverse(char byte) {
	uint8_t maskUp = 0x01;
	uint8_t maskDown = 0x80;
	uint8_t result = 0;
	for (; maskDown; maskDown >>= 1, maskUp <<= 1) {
		if (byte & maskUp)
			result |= maskDown;
	}
	return result;
}

static char convertTo8Bit2sComplement(uint8_t sign, uint8_t value) {
	if (sign) {
		if (value & 0x80) {
			return value;
		}
		else {
			return 0x80;
		}
	}
	else if (value > 0x7F) {
		return 0x7F;
	}
	else {
		return value;
	}
}

static void handleStream(char c) {
	static char previous4thByte = 0;
	static uint8_t skipped = 0;
	rxArray[bytesReceived] = c;
	bytesReceived++;
	if (bytesReceived == 3) {
		
		if (mouseType == '3') {
			// no fourth byte coming if not wheeled mouse
			bytesReceived = 0;
			// in theory buffer might run out in 1200 baud mode
			if (getFreeBuffer() < 3)
				return;
		} else if (getFreeBuffer() < 4 ) {
			skipped = 1;
			return;
		} else {
			skipped = 0;
		}
		
		// if movement is too large, just use maximum value that fits (two's complement!)
		uint8_t xSign = rxArray[0] & 0x10;
		uint8_t ySign = rxArray[0] & 0x20;
		char xMovement = convertTo8Bit2sComplement(xSign, rxArray[1]);
		char yMovement = (int)convertTo8Bit2sComplement(ySign, rxArray[2]) * -1;
		char byte1 = (1<<6) | ((rxArray[0] & 0x01)<<5) | ((rxArray[0] & 0x02)<<3) | ((yMovement & 0xC0)>>4) | ((xMovement & 0xC0)>>6);
		char byte2 = xMovement & 0x3F;
		char byte3 = yMovement & 0x3F;
		char byte4 = (rxArray[0] & 0x04) << 3;
		
		addTxData(byte1);
		addTxData(byte2);
		addTxData(byte3);
		
		if (mouseType == '3' && (byte4 || previous4thByte)) {
			addTxData(byte4);
			previous4thByte = byte4;
		}
		startTx();
	}
	else if (bytesReceived == 4) {
		bytesReceived = 0;
		//shouldn't happen but check just in case
		if (skipped)
			return;
			
		char byte4 = ((rxArray[0] & 0x04) << 2) | (convertTo8Bit2sComplement(rxArray[3] & 0xF0, rxArray[3] << 4) >> 4);
		addTxData(byte4);
		startTx();
	}
}

static inline void clearFlagsEnableStartConditionDetection() {
	USISR = (1<<USISIF) | (1<<USIOIF);
	USICR = (1<<USISIE) | (1<<USIWM1) | (1<<USICS1);
}

static void waitUntilPinDown(uint8_t mask) {
	uint8_t timeOut = 255;
	do {
		_delay_us(1);
		timeOut--;
	}
	while (timeOut && (PINB & mask));
}

static void handleTxEnd() {
	//release SDA
	DDRB = DDRB & ~(1<<PB5);
	
	//disable overflow and start condition interrupts, enable interrupts for other stuff
	USICR = (1<<USIWM1) | (1<<USICS1);
	sei();
	
	// wait so that SDA has had time to rise
	_delay_us(5);
	
	//wait until SDA low
	waitUntilPinDown(1<<PB5);
	
	//wait until SCL low
	waitUntilPinDown(1<<PB7);
	
	//wait until SDA and SCL released
	for(uint8_t i = 0; i<255; i++) {
		_delay_us(1);
		if ((PINB & (1<<PB5)) && (PINB & (1<<PB7))) {
			break;
		}
	}
	
	clearFlagsEnableStartConditionDetection();
}

static void handleTx2ndByte() {
	USIDR = parityAndStopBits;
	//set counter so it overflows after remaining bits and clear overflow flags
	USISR = (1<<USISIF) | (1<<USIOIF) | 13;
	handleOverflow = &handleTxEnd;
}

static void startPs2Tx(char c) {
	
	//clear status flags and counter, disable overflow and start condition interrupt, enable interrupts for other stuff
	//change to rising edge mode
	USISR = (1<<USISIF) | (1<<USIOIF);
	USICR = (1<<USIWM1) | (1<<USICS1);
	USIDR=0xFF;
	handleRxByte = &handleResponse;
	previousSentByte = c;
	sei();
	
	// some delay just in case
	_delay_us(10);
	
	//set SCL as output, bring SCL down for 100us
	PORTB = PORTB & ~(1<<PB7);
	DDRB = DDRB | (1<<PB7);
	_delay_us(100);
	
	char data = reverse(c);
	
	//odd parity needed
	if (parity_even_bit(data))
		parityAndStopBits = 0b01111111;
	else
		parityAndStopBits = 0b11111111;
	
	//bring SDA low
	PORTB = PORTB & ~(1<<PB5);
	DDRB = DDRB | (1<<PB5);
	_delay_us(10);
	//release SCL
	DDRB = DDRB & ~(1<<PB7);
	_delay_us(10);
	
	USIDR = data;
	
	//wait until SCL is low
	for(uint16_t i = 0; i<5100; i++) {
		_delay_us(3);
		if (!(PINB & (1<<PB7))) {
			_delay_us(3);
			break;
		} else if (i > 5000) {
			//mouse didn't take clk low within 15ms, do a hard reset to it
			DDRB = DDRB & ~(1<<PB5);
			resetMouse();
			return;
		}
	}
	
	//set PORTB5 to 1, so that SDA value depends on shift register
	PORTB = PORTB | (1<<PB5);
	
	//clear status flags and counter, enable overflow interrupt
	USISR = (1<<USISIF) | (1<<USIOIF);
	USICR = (1<<USIOIE) | (1<<USIWM1) | (1<<USICS1);
	handleOverflow = &handleTx2ndByte;
}

static void setSampleRate() {
	add(&ps2TxBuf, sampleRate);
	add(&ps2TxBuf, 0xF4);
	startPs2Tx(0xF3);
}

static void handleInit(char c) {
	if (c == 0xAA) {
		//enable timer1
		TCCR1B = (1<<WGM12) | (1<<CS12) | (1<<CS10);
	} else if (c == 0x00) {
		mouseType = '3';
		
		//check if wheel mouse only if requested sample rate is higher than 40.
		//using wheel in 1200 baud mode would not make sense, as it would
		//require dropping to 20 samples
		if (sampleRate == 40) {
			setSampleRate();
			return;
		}
		
		add(&ps2TxBuf, 200);
		add(&ps2TxBuf, 0xF3);
		add(&ps2TxBuf, 100);
		add(&ps2TxBuf, 0xF3);
		add(&ps2TxBuf, 80);
		add(&ps2TxBuf, 0xF2);
		startPs2Tx(0xF3);
	} else {
		//shoudn't happen, reset mouse with command
		startPs2Tx(0xFF);
	}
}

static void handleResponse(char c) {
	if (c == 0xFA) {
		if (elements(&ps2TxBuf) > 0) {
			startPs2Tx(remove(&ps2TxBuf));
		} else if (previousSentByte == 0xFF) {
			handleRxByte = &handleInit;
		} else if (previousSentByte != 0xF2) {
			handleRxByte = &handleStream;
		}
	} else if (c == 0xFC) {
		resetMouse();
	} else if (c == 0x00) {
		//set sample rate and enable streaming
		setSampleRate();
	} else if (c == 0x03) {
		mouseType = 'Z';
		setSampleRate();
	} else if (c == 0xFE) {
		startPs2Tx(previousSentByte);
	}
}

void ps2_init(uint8_t sampleRate_) {
	
	//init timer1 for interrupt every 2s, but don't start it yet
	OCR1A = (uint16_t)((F_CPU) * 2.0 / 1024 + 0.5);
	TCCR1B = 1 << WGM12;
	
	TIMSK |= 1 << OCIE1A;

	sampleRate = sampleRate_;
	init_buf(&ps2TxBuf, PS2_TX_BUF_SIZE, ps2TxBuf_array);
	
	clearFlagsEnableStartConditionDetection();
	handleRxByte = &handleInit;
}

void resetMouse() {
	mouseType = 0;
	firstByteReceived = 0;
	bytesReceived = 0;
	empty(&ps2TxBuf);
	
	//stop timer1
	TCCR1B = 1 << WGM12;
	
	//don't do anything else if power is already off due to overcurrent protection
	if (PORTB & (1<<PB4))
		return;
		
	//turn off power
	PORTB = PORTB | (1<<PB4);
	_delay_ms(500);
	clearFlagsEnableStartConditionDetection();
	PORTB = PORTB & ~(1<<PB4);
	handleRxByte = &handleInit;
}

char getMouseType() {
	return mouseType;
}

static void handleRx() {
	static char previousByte = 0;
	static uint8_t retryCount = 0;
	if (firstByteReceived) {
		//second byte still has most significant bit, parity and stop bit in it
		char secondByte = USIDR;
		char receivedData = (previousByte << 1) + ((secondByte & 0x04) >> 2);
		clearFlagsEnableStartConditionDetection();
		firstByteReceived = 0;
		if (parity_even_bit(receivedData) == (secondByte & 0x02) >> 1) {
			if (retryCount < 3) {
				startPs2Tx(0xFE); //ask for resend
				retryCount++;
			} else {
				retryCount = 0;
				resetMouse();
			}
		} else {
			retryCount = 0;
			handleRxByte(reverse(receivedData));
		}
	} else {
		//set counter so it overflows after remaining bits and clear overflow flags
		USISR = (1<<USISIF) | (1<<USIOIF) | 10;
		previousByte = USIDR;
		firstByteReceived = 1;
	}
}

ISR(USI_START_vect) {
	//wait until SCL low. if timings are tight, might not arrive here until it's already gone down
	waitUntilPinDown(1<<PB7);
	
	//clear counter and start condition interrupt flag, enable overflow interrupt
	//set counter to 1 because we already waited until SCL got low
	USISR = (1<<USISIF) | (1<<USIOIF) | 1;
	USICR = (1<<USIOIE) | (1<<USIWM1) | (1<<USICS1);
	handleOverflow = &handleRx;
}

ISR(USI_OVERFLOW_vect) {
	//clear timer1 counter as mouse is alive
	TCNT1 = 0;
	handleOverflow();
}

ISR(TIMER1_COMPA_vect) {
	//discard previous bytes after 2s...
	bytesReceived = 0;
	firstByteReceived = 0;
	
	// send enable data reporting to check if mouse is still attached,
	// as it shouldn't do anything except make mouse reply an ack
	startPs2Tx(0xF4);
}