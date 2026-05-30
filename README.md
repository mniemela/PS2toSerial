# PS2toSerial
PS/2 to serial converter using ATtiny4313 and MAX232. There are plenty of similar projects, but almost all of them are external, so here's my internal version
which draws power from floppy power connector.

<img src="documentation/adapter.jpg" width="480">

## Description

This does essentially the same thing as other PS/2 to serial adapters: it interfaces with PS/2 mouse, and pretends to be a serial mouse to the computer.

It has following features:
* Support for normal 1200 baud serial, or 9600 baud high speed mode
* Support for wheel mouse
* Sample rate is selected from 20/40/60/100/200 with jumpers. For higher than 40, 9600 baud serial is needed.
* Resolution is configured with jumpers
* Option for USB A connector, so no need for a passive USB->PS/2 -adapter
* Option for PS/2 connector is also provided
* Supports hotplugging
* Crystal oscillator for ensuring accurate baud rate
* TVS diodes for ESD protection
* Overcurrent protection using ATtiny4313's onboard analog comparator and a P-type MOSFET as a switch
* No SMD components, so it's easy to assemble
* Programming via SPI header

Jumpers J1 sets baud rate, closed is high speed mode. J2 and J3 sets sample rate as described in silkscreen, and J4 and J4 set resolution.

Using 9600 baud serial speed requires modified mouse drivers, which can be found for example from this project:
https://github.com/LimeProgramming/USB-serial-mouse-adapter/tree/main

There are footprints for both USB-A and mini-DIN6 or PS/2 connector, only one of them should be populated.

## KVM issues

Some KVM switches ignore commands to set sample rate, which results in this adapter not working properly in 1200 baud mode. The adapter tries to set baud
rate to configured value, but the KVM will ignore it and packets are received faster than they can be sent to PC and some of them need to be dropped. 
This results in erratic mouse movement, but it can be avoided using high speed mode which you probably want to use anyways for reduced latency and
possibility for higher sample rate.

## Technical details

ATtiny has both hardware USART and Universal Serial Interface or USI, which is readily adaptable to PS/2, so there's only minimal need for bit banging. USI provides 
start condition detector with interrupt and counter (increased at PS/2's clock rising and falling edge) overflow interrupt, which removes need for polling. Instead
of sampling data on falling clock edge, this adapter uses rising edge as all the devices I have measured change data line state closer to falling than rising edge.
In particular, with a KVM switch this was within as little as 3µs from falling edge!

Resistors R8 and R9 form voltage divider for analog comparator positive input pin. R7 is used as a shunt resistor for current detection, voltage after it goes to
comparator's negative input pin. Comparator's output is monitored by interrupt from timer 0 every 100µs, and if there's overcurrent for 1ms, power to PS/2 is shut down.

RTS and DTR signals to pins PB2 and PB3 are monitored with pin change interrupt. When either of them goes down and then back up, mouse detection characters are sent if
mouse is connected.

Normally PS/2 host doesn't know whether mouse is disconnected or is just still, so the adapter polls mouse by sending command enable data reporting or 0xF4 every two
seconds. If mouse is connected, it replys with an ACK or 0xFA, if it's disconnected, there's no clock signal which is detected.

Some KVM switches randomly reset in the middle of normal operation and send 0xAA and 0x00 as during boot, then stopping to send movement data. This adapter attempts
to detect this situation and recover from it.

<img src="documentation/schema.png" width="640">

\
**BOM of specific components:**
| Ref. | Part. no. | Note |
|---|---|---|
| Q1 | MTD20P06HDL | Any DPAK logic level P-channel enhancement-type MOSFET with similar pinout will work. |
| U1 | ATtiny4313-PU | Microcontroller |
| U2 | MAX232 | RS-232 transceiver |
| R7 | 560mΩ resistor | Anything with max. dissipation > 600mW will work. |
| Y1 | AS-18.432-20 | Other frequencies up to 20M can be used, but will require modifications to software. |
| D2-D4 | P6KE6.8 | TVS diode with 5.8V working voltage |
| J10 | MDC-206 | mini-DIN6 connector |
| J12, J13 | AMT0440051DB0000G | M4 screw terminal |
| C6 | ??? | Exact value of this capacitor isn't important. |
