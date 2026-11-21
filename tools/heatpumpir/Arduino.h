// Just enough of the Arduino API to build arduino-heatpumpir on a PC.
// delay() records a space, as the IR LED is off while the sketch waits.
#ifndef IRLINGO_HOST_ARDUINO_H
#define IRLINGO_HOST_ARDUINO_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#define PROGMEM
#define PSTR(s) (s)
#define F(s) (s)
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))
#define pgm_read_byte_near(addr) (*(const uint8_t *)(addr))
#define pgm_read_word(addr) (*(const uint16_t *)(addr))
#define memcpy_P memcpy
#define strcmp_P strcmp
#define strlen_P strlen
#define sprintf_P sprintf

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1

typedef bool boolean;
typedef uint8_t byte;

void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);
unsigned long millis();
unsigned long micros();
inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t, uint8_t) {}
inline int digitalRead(uint8_t) { return 0; }

#endif
