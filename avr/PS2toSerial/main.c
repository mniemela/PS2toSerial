/*
 * PS2toSerial.c
 *
 * Created: 3.1.2026 8.05.48
 * Author : Mikael Niemelä
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <inttypes.h>

#include "constants.h"
#include "ps2.h"
#include "uart.h"
#include <util/delay.h>

void init() {
	
	//turn on pullups for jumpers and unused pins and enable driving output low for PD4
	PORTD = ~(1<<PD1);
	PORTB = 1 << PB6;
	
	//turn off comparator digital inputs
	DIDR = (1<<AIN1D) | (1<<AIN0D);
	
	_delay_us(100);
	
	// check jumpers for sample rate and baud rate
	uint8_t sampleRate = 0;
	uint8_t highSpeedUart = !((1 << PIND0) & PIND);
	if (highSpeedUart) {
		if (((1 << PIND3) & PIND) && ((1 << PIND4) & PIND) )
			sampleRate = 40;
		else if ((1 << PIND3) & PIND)
			sampleRate = 60;
		else if ((1 << PIND4) & PIND)
			sampleRate = 100;
		else
			sampleRate = 200;
	} else {
		if ((1 << PIND4) & PIND)
			sampleRate = 40;
		else
			sampleRate = 20;
	}
	
	//check jumpers for resolution
	uint8_t resolution = (((1 << PIND5) | (1 << PIND6)) & PIND) >> 5;
	
	
	uart_init(highSpeedUart);
	ps2_init(sampleRate, resolution, highSpeedUart);
	
	//turn on device power
	DDRB = 1<<PB4;
	
	//init timer0 for interrupt every 100µs
	OCR0A = (uint8_t)((F_CPU) * 0.0001 / 8 + 0.5);
	TCCR0A = (1 << COM0A1) | (1 << WGM01);
	TCCR0B = 1 << CS01;
	TIMSK |= (1 << OCIE0A);
	
	//init pin change interrupt for detecting RTS low
	PCMSK = (1<<PCINT2) | (1<<PCINT3);
	GIMSK = 1<<PCIE0;

	sei();
}

int main(void)
{
    init();
	
    while (1) 
    {
    }
}

ISR(PCINT0_vect) {
	//send detection characters when RTS or DTR goes back up
	static uint8_t previousState = 0;
	uint8_t currentState = PINB & ((1<<PB2) | (1<<PB3));
	uint8_t dtrValue = PINB & (1<<PB3);
	uint8_t rtsValue = PINB & (1<<PB2);
	if ((!dtrValue && (previousState & (1<<PB3))) || (!rtsValue && (previousState & (1<<PB2)))) {
		char mouseType = getMouseType();
		if (mouseType) {
			addTxData('M');
			addTxData(mouseType);
			//send fake PnP package, looks like this is required by MS wheel mouse driver
			addTxData(0x08);
			addTxData(0x09);
			startTx();
		}
	}
	if (currentState != previousState)
		previousState = currentState;
}

ISR(TIMER0_COMPA_vect) {
	//don't do anything if power is already off
	if (PORTB & (1<<PB4))
		return;
	
	// increase counter when comparator output is on, otherwise decrement (until 0)
	static uint8_t counter = 0;
	if (ACSR & (1 << ACO)) {
			counter++;
		} else {
		if (counter > 0)
			counter--;
	}
	
	// turn power off after 1ms of overcurrent
	if (counter > 10)
	PORTB = PORTB | (1<<PB4);
}
