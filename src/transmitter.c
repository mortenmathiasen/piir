#if HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_GPIOD_H

#include <string.h>
#include <stdlib.h>
#include <gpiod.h>
#include <unistd.h>
#include <sys/time.h>
#include "log.h"
#include "transmitter.h"

void addPulse(bool high, uint32_t duration, gpioPulse_t *irSignal, unsigned int *pulseCount)
{
  if (duration==0)
    return;
  int index = *pulseCount;

  log_trace("%s %d", high?"On ":"Off", duration);

  if (irSignal[index-1].high == high)
    {
      irSignal[index-1].usDelay += duration;
    }
  else
    {
      irSignal[index].high = high;
      irSignal[index].usDelay = duration;
      (*pulseCount)++;
    }
}

// Generates a square wave for duration (microseconds) at frequency (Hz)
// dutyCycle is a floating value between 0 and 1.
void addMark(unsigned long frequency, double dutyCycle, unsigned long duration, gpioPulse_t *irSignal, unsigned int *pulseCount)
{
  log_trace("Add mark   %d", duration);
  double cycleLength = (double)1000000/frequency;
  unsigned totalCycles = 0.5f+((double)(duration/cycleLength));
  unsigned long actualUsedDuration = 0;
  double calculatedUsedDuration = 0;
  for (unsigned i = 0; i < totalCycles; i++)
    {
      calculatedUsedDuration += cycleLength;
      unsigned long thisCycleDuration = 0.5f + calculatedUsedDuration-actualUsedDuration;
      actualUsedDuration += thisCycleDuration;

      // High pulse
      unsigned onDuration = thisCycleDuration * dutyCycle;
      addPulse(1, onDuration, irSignal, pulseCount);

      // Low pulse
      unsigned offDuration = thisCycleDuration - onDuration;
      addPulse(0, offDuration, irSignal, pulseCount);
    }
}
		
// Generates a low signal gap for duration, in microseconds, on GPIO pin outPin
void addSpace(unsigned long duration, gpioPulse_t *irSignal, unsigned int *pulseCount)
{
  log_trace("Add space  %d", duration);
  addPulse(0, duration, irSignal, pulseCount);
}

unsigned long microtime() {
  struct timeval tv;
  return 1000000 * tv.tv_sec + tv.tv_usec;
}

// Transmit generated wave using available library libgpiod
int transmitWave(uint32_t outPin, gpioPulse_t *irSignal, unsigned int *pulseCount)
{
  unsigned int offsets[1];

  int values[1];
  int err;

  struct gpiod_chip *chip;
  struct gpiod_line_request_config config;
  struct gpiod_line_bulk lines;
  
  // get chip holding the output pin
  chip = gpiod_chip_open("/dev/gpiochip0");
  if(!chip)
  {
    log_error("gpiod_chip_open");
    goto cleanup;
  }

  // get output line for pin 
  offsets[0] = outPin;
  err = gpiod_chip_get_lines(chip, offsets, 1, &lines);
  if(err)
  {
    log_error("gpiod_chip_get_lines");
    goto cleanup;
  }

  // Setup output configuration
  memset(&config, 0, sizeof(config));
  config.consumer = "piir";
  config.request_type = GPIOD_LINE_REQUEST_DIRECTION_OUTPUT;
  config.flags = 0;

  // get the bulk line setting default value to 0
  values[0] = 0;
  err = gpiod_line_request_bulk(&lines, &config, values);
  if(err)
  {
    log_error("gpiod_line_request_bulk");
    goto cleanup;
  }

  unsigned int i;
  time_t old_microtime, new_microtime;
  for (i = 0, old_microtime=microtime(), new_microtime=old_microtime ;
       i < *pulseCount ;
       i++, old_microtime=new_microtime, new_microtime=microtime() ) {

    // Change value to next pulse signal
    values[0] = irSignal[i].high;
    err = gpiod_line_set_value_bulk(&lines, values);
    if(err) {
      log_error("gpiod_line_set_value_bulk");
      goto cleanup;
    }

    // Sleep until pulse is finished
    long delay = irSignal[i].usDelay-(new_microtime-old_microtime);
    log_debug("Delay %d", delay);
    err = usleep(delay);
    if(err) {
        log_error("usleep error");
        goto cleanup;
    }

  }

  // Ensure turn off IR led
  values[0] = 0;
  err = gpiod_line_set_value_bulk(&lines, values);
  if(err) {
        log_error("nanosleep interrupted");
        goto cleanup;
  }

cleanup:
  gpiod_line_release_bulk(&lines);
  gpiod_chip_close(chip);

  return EXIT_SUCCESS;
}

int irSlingGeneric(uint32_t outPin,
		   int frequency,
		   double dutyCycle,
		   size_t sdDefCount,
		   symbolDefinition *sdDef,
		   const char *code)
{
  if (outPin > 31)
    {
      // Invalid pin number
      return 1;
    }

  size_t codeLen = strlen(code);

  log_info("code length is %zu", codeLen);
  log_info("%s", code);

  if (codeLen > MAX_COMMAND_SIZE)
    {
      log_fatal("Code length greater than %d", MAX_COMMAND_SIZE);
      // Command is too big
      return 1;
    }

  gpioPulse_t irSignal[MAX_PULSES];
  unsigned int pulseCount = 0;

  log_debug("Build pulses");
  size_t i = 0;
  for (i = 0; i < codeLen; i++)
    {
      unsigned long markDuration = 0;
      unsigned long spaceDuration = 0;
      symbolDefinition *symdef = 0;
      size_t j =  0;
      for (j = 0 ; j<sdDefCount ; j++) {
	if (sdDef[j].symbol==code[i]) {
	  symdef = &sdDef[j];
	  markDuration = symdef->markDuration;
	  spaceDuration = symdef->spaceDuration;
	}
      }
      if (!symdef) {
	log_fatal("Undefined symbol '%c'", code[i]);
	return 1;
      }

      log_trace("Pulse %d", pulseCount);
      addMark(frequency, dutyCycle, markDuration, irSignal, &pulseCount);
      addSpace(spaceDuration, irSignal, &pulseCount);
    }

  log_trace("pulse count is %i\n", pulseCount);
  // End Generate Code

  log_debug("Transmit pulses");
  return transmitWave(outPin, irSignal, &pulseCount);
}

#endif
