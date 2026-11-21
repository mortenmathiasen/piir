// A kernel IR driver that accepts everything, linked into irlingo_fake_lirc
// with -Wl,--wrap so complete irlingo runs succeed without hardware. The
// device file named in the remote is a regular file receiving the pulses.
#include <stdarg.h>
#include <stdint.h>
#include <linux/lirc.h>

static int fakeIoctl(unsigned long request, uint32_t *arg) {
  if (request == LIRC_GET_FEATURES)
    *arg = LIRC_CAN_SEND_PULSE | LIRC_CAN_SET_SEND_CARRIER | LIRC_CAN_SET_SEND_DUTY_CYCLE;
  return 0;
}

int __wrap_ioctl(int fd, unsigned long request, ...) {
  (void)fd;
  va_list ap;
  va_start(ap, request);
  uint32_t *arg = va_arg(ap, uint32_t *);
  va_end(ap);
  return fakeIoctl(request, arg);
}

int __wrap___ioctl_time64(int fd, unsigned long request, ...) {
  (void)fd;
  va_list ap;
  va_start(ap, request);
  uint32_t *arg = va_arg(ap, uint32_t *);
  va_end(ap);
  return fakeIoctl(request, arg);
}
