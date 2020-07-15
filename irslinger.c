#include <string.h>
#include <stdlib.h>
#include <pigpio.h>
#include "log.h"
#include "irslinger.h"

 void addPulse(uint32_t onPins, uint32_t offPins, uint32_t duration, gpioPulse_t *irSignal, unsigned int *pulseCount)
{
  int index = *pulseCount;

  log_trace("%s %d", onPins?"On ":offPins?"Off":"?", duration);
  if ((!onPins && !offPins) || (onPins && offPins)) {
      log_fatal("%s %d", onPins?"On ":offPins?"Off":"?", duration);
      abort();
  }

  if (irSignal[index-1].gpioOn == onPins &&
      irSignal[index-1].gpioOff == offPins)
    {
      irSignal[index-1].usDelay += duration;
    }
  else
    {
      irSignal[index].gpioOn = onPins;
      irSignal[index].gpioOff = offPins;
      irSignal[index].usDelay = duration;
      (*pulseCount)++;
    }
}

// Generates a square wave for duration (microseconds) at frequency (Hz)
// on GPIO pin outPin. dutyCycle is a floating value between 0 and 1.
void addMark(uint32_t outPin, unsigned long frequency, double dutyCycle, unsigned long duration, gpioPulse_t *irSignal, unsigned int *pulseCount)
{
  log_debug("Add mark   %d", duration);
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
      addPulse(1 << outPin, 0, onDuration, irSignal, pulseCount);

      // Low pulse
      unsigned offDuration = thisCycleDuration - onDuration;
      addPulse(0, 1 << outPin, offDuration, irSignal, pulseCount);
    }
}
		
// Generates a low signal gap for duration, in microseconds, on GPIO pin outPin
 void addSpace(uint32_t outPin, unsigned long duration, gpioPulse_t *irSignal, unsigned int *pulseCount)
{
  log_debug("Add space  %d", duration);
  addPulse(0, 1 << outPin, duration, irSignal, pulseCount);
}

// Transmit generated wave
 int transmitWave(uint32_t outPin, gpioPulse_t *irSignal, unsigned int *pulseCount)
{
  // Prepare DMA channels
  gpioCfgDMAchannels(5, 4);
  
  // Init pigpio
  if (gpioInitialise() < 0)
    {
      // Initialization failed
      printf("GPIO Initialization failed\n");
      return 1;
    }

  // Setup the GPIO pin as an output pin
  gpioSetMode(outPin, PI_OUTPUT);

  // Start a new wave
  gpioWaveClear();

  if (log_get_level() <= LOG_TRACE) {
    for (int i ; i < *pulseCount ; i++)
      log_trace("%s=%zu", irSignal[i].gpioOn?"On":"Off", irSignal[i].usDelay);
  }
  
  int result = gpioWaveAddGeneric(*pulseCount, irSignal);
  if (result==PI_TOO_MANY_PULSES)
    {
      log_fatal("gpioWaveAddGeneric: PI_TOO_MANY_PULSES");
      abort();
    }
  else if (result!=*pulseCount)
    {
      log_debug("gpioWaveAddGeneric: added %zu different from expected %zu", result, *pulseCount);
      abort();
    }
  int waveID = gpioWaveCreate();

  if (waveID >= 0)
    {
      int result = gpioWaveTxSend(waveID, PI_WAVE_MODE_ONE_SHOT);
      if (result==PI_BAD_WAVE_ID)
	log_error("gpioWaveTxSend ERROR: PI_BAD_WAVE_ID");
      else if (result==PI_BAD_WAVE_MODE)
	log_error("gpioWaveTxSend ERROR: PI_BAD_WAVE_MODE");
      else
	printf("gpioWaveTxSend OK: %i\n", result);
    }
  else
    {
      printf("Wave creation failure!\n %i", waveID);
    }

  // Wait for the wave to finish transmitting
  time_sleep(0.2);
  while (gpioWaveTxBusy())
    {
      time_sleep(0.1);
    }

  // Delete the wave if it exists
  if (waveID >= 0)
    {
      gpioWaveDelete(waveID);
    }

  // Cleanup
  gpioTerminate();
  return 0;
}

/* int irSlingRC5(uint32_t outPin,
  int frequency,
  double dutyCycle,
  int pulseDuration,
  const char *code)
  {
  if (outPin > 31)
  {
  // Invalid pin number
  return 1;
  }

  size_t codeLen = strlen(code);

  printf("code size is %zu\n", codeLen);

  if (codeLen > MAX_COMMAND_SIZE)
  {
  // Command is too big
  return 1;
  }

  gpioPulse_t irSignal[MAX_PULSES];
  int pulseCount = 0;

  // Generate Code
  int i;
  for (i = 0; i < codeLen; i++)
  {
  if (code[i] == '0')
  {
  addMark(outPin, frequency, dutyCycle, pulseDuration, irSignal, &pulseCount);
  addSpace(outPin, pulseDuration, irSignal, &pulseCount);
  }
  else if (code[i] == '1')
  {
  addSpace(outPin, pulseDuration, irSignal, &pulseCount);
  addMark(outPin, frequency, dutyCycle, pulseDuration, irSignal, &pulseCount);
  }
  else
  {
  printf("Warning: Non-binary digit in command\n");
  }
  }

  printf("pulse count is %i\n", pulseCount);
  // End Generate Code

  return transmitWave(outPin, irSignal, &pulseCount);
  }

   int irSling(uint32_t outPin,
  int frequency,
  double dutyCycle,
  int leadingPulseDuration,
  int leadingGapDuration,
  int onePulse,
  int zeroPulse,
  int oneGap,
  int zeroGap,
  int sendTrailingPulse,
  const char *code)
  {
  if (outPin > 31)
  {
  // Invalid pin number
  return 1;
  }

  size_t codeLen = strlen(code);

  printf("code size is %zu\n", codeLen);

  if (codeLen > MAX_COMMAND_SIZE)
  {
  // Command is too big
  return 1;
  }

  gpioPulse_t irSignal[MAX_PULSES];
  int pulseCount = 0;

  // Generate Code
  addMark(outPin, frequency, dutyCycle, leadingPulseDuration, irSignal, &pulseCount);
  addSpace(outPin, leadingGapDuration, irSignal, &pulseCount);

  int i;
  for (i = 0; i < codeLen; i++)
  {
  if (code[i] == '0')
  {
  addMark(outPin, frequency, dutyCycle, zeroPulse, irSignal, &pulseCount);
  addSpace(outPin, zeroGap, irSignal, &pulseCount);
  }
  else if (code[i] == '1')
  {
  addMark(outPin, frequency, dutyCycle, onePulse, irSignal, &pulseCount);
  addSpace(outPin, oneGap, irSignal, &pulseCount);
  }
  else
  {
  printf("Warning: Non-binary digit in command\n");
  }
  }

  if (sendTrailingPulse)
  {
  addMark(outPin, frequency, dutyCycle, onePulse, irSignal, &pulseCount);
  }

  printf("pulse count is %i\n", pulseCount);
  // End Generate Code

  return transmitWave(outPin, irSignal, &pulseCount);
  }

   int irSlingRaw(uint32_t outPin,
  int frequency,
  double dutyCycle,
  const int *pulses,
  int numPulses)
  {
  if (outPin > 31)
  {
  // Invalid pin number
  return 1;
  }

  // Generate Code
  gpioPulse_t irSignal[MAX_PULSES];
  int pulseCount = 0;

  int i;
  for (i = 0; i < numPulses; i++)
  {
  if (i % 2 == 0) {
  addMark(outPin, frequency, dutyCycle, pulses[i], irSignal, &pulseCount);
  } else {
  addSpace(outPin, pulses[i], irSignal, &pulseCount);
  }
  }

  printf("pulse count is %i\n", pulseCount);
  // End Generate Code

  return transmitWave(outPin, irSignal, &pulseCount);
  } 
*/

int irSlingGeneric(uint32_t outPin,
		   int frequency,
		   double dutyCycle,
		   symbolDefinition sdDef[],
		   const char *code)
{
  if (outPin > 31)
    {
      // Invalid pin number
      return 1;
    }

  size_t codeLen = strlen(code);

  log_debug("code length is %zu", codeLen);
  log_debug("%s", code);

  if (codeLen > MAX_COMMAND_SIZE)
    {
      log_fatal("Code length greater than %d", MAX_COMMAND_SIZE);
      // Command is too big
      return 1;
    }

  gpioPulse_t irSignal[MAX_PULSES];
  unsigned int pulseCount = 0;

  int i;
  for (i = 0; i < codeLen; i++)
    {
      unsigned long markDuration = 0;
      unsigned long spaceDuration = 0;
      for (int j = 0 ; j<sizeof(codeLen) ; j++) {
	if (sdDef[j].symbol==code[i]) {
	  markDuration = sdDef[j].markDuration;
	  spaceDuration = sdDef[j].spaceDuration;
	}
      }
      if (markDuration == spaceDuration &&  spaceDuration == 0) {
	log_debug("Undefined symbol '%c'", code[i]);
	return 1;
      }

      log_debug("Pulse %d", pulseCount);
      addMark(outPin, frequency, dutyCycle, markDuration, irSignal, &pulseCount);
      addSpace(outPin, spaceDuration, irSignal, &pulseCount);
    }

  log_debug("pulse count is %i\n", pulseCount);
  // End Generate Code

  return transmitWave(outPin, irSignal, &pulseCount);
}
