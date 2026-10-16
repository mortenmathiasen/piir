#ifdef HAVE_LINUX_LIRC_H

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/lirc.h>
#include "log.h"
#include "config.h"
#include "irgeneric.h"

// Maximum number of values the kernel accepts in one write (LIRCBUF_SIZE)
#define MAX_LIRC_VALUES 1024

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

  if (*count >= MAX_LIRC_VALUES)
    {
      log_fatal("Code needs more than %d pulses and spaces", MAX_LIRC_VALUES);
      return 1;
    }

  buffer[(*count)++] = duration;
  return 0;
}

// Transmit using the kernel IR transmitter driver (e.g. dtoverlay=gpio-ir-tx).
// The GPIO pin is set by the device tree overlay, so outPin is not used.
int irSlingGeneric(uint32_t outPin,
		   int frequency,
		   double dutyCycle,
		   size_t sdDefCount,
		   symbolDefinition *sdDef,
		   const char *code)
{
  size_t codeLen = strlen(code);

  log_info("code length is %zu", codeLen);
  log_info("%s", code);

  if (codeLen > MAX_COMMAND_SIZE)
    {
      log_fatal("Code length greater than %d", MAX_COMMAND_SIZE);
      return 1;
    }

  unsigned int buffer[MAX_LIRC_VALUES];
  size_t count = 0;

  log_debug("Build pulses");
  for (size_t i = 0; i < codeLen; i++)
    {
      symbolDefinition *symdef = 0;
      for (size_t j = 0 ; j < sdDefCount ; j++)
	if (sdDef[j].symbol == code[i])
	  symdef = &sdDef[j];
      if (!symdef)
	{
	  log_fatal("Undefined symbol '%c'", code[i]);
	  return 1;
	}

      if (addDuration(1, symdef->markDuration, buffer, &count) ||
	  addDuration(0, symdef->spaceDuration, buffer, &count))
	return 1;
    }

  // The kernel requires the signal to end with a pulse
  if (count % 2 == 0 && count > 0)
    count--;
  if (count == 0)
    {
      log_fatal("Nothing to transmit");
      return 1;
    }
  log_trace("pulse count is %zu", count);

  const char *device = 0;
  loadDevice(&device);
  if (!device)
    device = LIRC_DEVICE;
  log_debug("Transmit pulses on %s (GPIO pin is set by the device tree overlay, config outpin is %u)", device, outPin);

  int fd = open(device, O_WRONLY);
  if (fd < 0)
    {
      int openError = errno;
      log_fatal("Cannot open %s: %s", device, strerror(openError));
      if (openError == ENOENT)
	log_fatal("Enable the kernel IR transmitter by adding 'dtoverlay=gpio-ir-tx,gpio_pin=%u' "
		  "to /boot/firmware/config.txt and reboot", outPin);
      else if (openError == EACCES)
	log_fatal("Run piir with sudo or add your user to the group owning %s", device);
      return 1;
    }

  int result = 1;
  uint32_t features = 0;
  if (ioctl(fd, LIRC_GET_FEATURES, &features) || !(features & LIRC_CAN_SEND_PULSE))
    {
      log_fatal("%s cannot transmit", device);
      goto cleanup;
    }

  uint32_t mode = LIRC_MODE_PULSE;
  if (ioctl(fd, LIRC_SET_SEND_MODE, &mode))
    {
      log_fatal("LIRC_SET_SEND_MODE: %s", strerror(errno));
      goto cleanup;
    }

  if (features & LIRC_CAN_SET_SEND_CARRIER)
    {
      uint32_t carrier = frequency;
      if (ioctl(fd, LIRC_SET_SEND_CARRIER, &carrier))
	{
	  log_fatal("LIRC_SET_SEND_CARRIER %u: %s", carrier, strerror(errno));
	  goto cleanup;
	}
    }
  else
    log_warn("%s cannot set carrier frequency", device);

  if (features & LIRC_CAN_SET_SEND_DUTY_CYCLE)
    {
      uint32_t duty = dutyCycle * 100 + 0.5;
      if (duty < 1)
	duty = 1;
      if (duty > 99)
	duty = 99;
      if (ioctl(fd, LIRC_SET_SEND_DUTY_CYCLE, &duty))
	{
	  log_fatal("LIRC_SET_SEND_DUTY_CYCLE %u: %s", duty, strerror(errno));
	  goto cleanup;
	}
    }
  else
    log_warn("%s cannot set duty cycle", device);

  ssize_t size = count * sizeof(buffer[0]);
  ssize_t written = write(fd, buffer, size);
  if (written != size)
    {
      log_fatal("Write to %s failed: %s", device, written < 0 ? strerror(errno) : "short write");
      goto cleanup;
    }

  result = 0;

cleanup:
  close(fd);
  return result;
}

#endif
