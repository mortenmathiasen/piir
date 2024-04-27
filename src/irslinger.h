#ifndef IRSLINGER_H
#define IRSLINGER_H

#include "symboldefinition.h"
#include <stdint.h>

typedef struct
{
   uint32_t gpioOn;
   uint32_t gpioOff;
   uint32_t usDelay;
} gpioPulse_t;

// #include "pigpio.h"

#define MAX_COMMAND_SIZE 512
#define MAX_PULSES 12000

 void addPulse(uint32_t onPins, uint32_t offPins, uint32_t duration, gpioPulse_t *irSignal, unsigned int *pulseCount);

// Generates a square wave for duration (microseconds) at frequency (Hz)
// on GPIO pin outPin. dutyCycle is a floating value between 0 and 1.
 void addMark(uint32_t outPin, unsigned long frequency, double dutyCycle, unsigned long duration, gpioPulse_t *irSignal, unsigned int *pulseCount);
		
// Generates a low signal gap for duration, in microseconds, on GPIO pin outPin
 void addSpace(uint32_t outPin, unsigned long duration, gpioPulse_t *irSignal, unsigned int *pulseCount);

int irSlingGeneric(uint32_t outPin,
		   int frequency,
		   double dutyCycle,
		   size_t sdDefCount,
		   symbolDefinition *sdDef,
		   const char *code);
#endif
