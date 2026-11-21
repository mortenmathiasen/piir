#ifndef PULSES_H
#define PULSES_H

#include <stddef.h>
#include "symboldefinition.h"

// Longest code, in symbols
#define MAX_COMMAND_SIZE 2048

// Most pulses and spaces in one code (the Linux kernel's LIRCBUF_SIZE)
#define MAX_PULSE_VALUES 1024

// Convert a code (a string of symbols) to alternating pulse and space
// durations in microseconds, starting with a pulse and ending with one.
// buffer holds MAX_PULSE_VALUES values; their number is returned in *count.
// Returns 0 on success, 1 for an undefined symbol, a code needing more than
// MAX_PULSE_VALUES values or nothing to send.
int irlingoBuildPulses(size_t sdDefCount, const symbolDefinition *sdDef,
                       const char *code, unsigned int *buffer, size_t *count);

#endif
