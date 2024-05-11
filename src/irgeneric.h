#ifndef IRGENERIC_H
#define IRGENERIC_H

#include "symboldefinition.h"
#include <stdint.h>

#define MAX_COMMAND_SIZE 512
#define MAX_PULSES 12000

extern int irSlingGeneric(uint32_t outPin,
		   int frequency,
		   double dutyCycle,
		   size_t sdDefCount,
		   symbolDefinition *sdDef,
		   const char *code);
#endif
