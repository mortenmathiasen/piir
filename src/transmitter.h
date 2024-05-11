#ifndef TRANSMITTER_H
#define TRANSMITTER_H

#include <stdbool.h>
#include "irgeneric.h"

typedef struct
{
   bool high;
   uint32_t usDelay;
} gpioPulse_t;

void addPulse(bool high, uint32_t duration, gpioPulse_t *irSignal, unsigned int *pulseCount);

// Generates a square wave for duration (microseconds) at frequency (Hz)
// on GPIO pin outPin. dutyCycle is a floating value between 0 and 1.
 void addMark(unsigned long frequency, double dutyCycle, unsigned long duration, gpioPulse_t *irSignal, unsigned int *pulseCount);
		
// Generates a low signal gap for duration, in microseconds, on GPIO pin outPin
void addSpace(unsigned long duration, gpioPulse_t *irSignal, unsigned int *pulseCount);

#endif
