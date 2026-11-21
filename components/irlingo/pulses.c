#include <string.h>
#include <stdlib.h>
#include "log.h"
#include "pulses.h"

// Append a pulse (mark) or space to the buffer, merging with the previous
// value of the same kind. Buffer index 0, 2, 4... are pulses, 1, 3, 5... spaces.
static int addDuration(int pulse, unsigned duration, unsigned int *buffer, size_t *count)
{
  if (duration == 0)
    return 0;

  // A space cannot come first
  if (*count == 0 && !pulse)
    return 0;

  int lastIsPulse = *count % 2 == 1;
  if (*count > 0 && lastIsPulse == pulse)
    {
      buffer[*count-1] += duration;
      return 0;
    }

  if (*count >= MAX_PULSE_VALUES)
    {
      log_fatal("Code needs more than %d pulses and spaces", MAX_PULSE_VALUES);
      return 1;
    }

  buffer[(*count)++] = duration;
  return 0;
}

// Convert a symbol string to alternating pulse and space durations, as the
// Linux kernel's IR drivers and ESPHome's remote_transmitter take them.
// Returns 0 on success.
int irlingoBuildPulses(size_t sdDefCount,
		    const symbolDefinition *sdDef,
		    const char *code,
		    unsigned int *buffer,
		    size_t *count)
{
  *count = 0;
  for (size_t i = 0; code[i]; i++)
    {
      const symbolDefinition *symdef = 0;
      for (size_t j = 0 ; j < sdDefCount ; j++)
	if (sdDef[j].symbol == code[i])
	  symdef = &sdDef[j];
      if (!symdef)
	{
	  log_fatal("Undefined symbol '%c'", code[i]);
	  return 1;
	}

      if (addDuration(1, symdef->markDuration, buffer, count) ||
	  addDuration(0, symdef->spaceDuration, buffer, count))
	return 1;
    }

  // The kernel requires the signal to end with a pulse
  if (*count % 2 == 0 && *count > 0)
    (*count)--;
  if (*count == 0)
    {
      log_fatal("Nothing to transmit");
      return 1;
    }
  return 0;
}
