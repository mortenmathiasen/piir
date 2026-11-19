// The lirc backend talking to a device. The device is a regular file, and
// ioctl() and write() are replaced at link time (-Wl,--wrap) so every
// answer of the kernel driver can be simulated.
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/lirc.h>
#include "unittest.h"
#include "irgeneric.h"
#include "config.h"
#include "log.h"

// --- simulated driver
static uint32_t fakeFeatures;
static unsigned long failRequest;    // ioctl request that fails, 0 for none
static uint32_t lastMode, lastCarrier, lastDuty;
static int shortWrite;

int __real_ioctl(int fd, unsigned long request, ...);
int __wrap_ioctl(int fd, unsigned long request, ...) {
  (void)fd;
  va_list ap;
  va_start(ap, request);
  uint32_t *arg = va_arg(ap, uint32_t *);
  va_end(ap);
  if (request == failRequest) {
    errno = EINVAL;
    return -1;
  }
  switch (request) {
  case LIRC_GET_FEATURES: *arg = fakeFeatures; return 0;
  case LIRC_SET_SEND_MODE: lastMode = *arg; return 0;
  case LIRC_SET_SEND_CARRIER: lastCarrier = *arg; return 0;
  case LIRC_SET_SEND_DUTY_CYCLE: lastDuty = *arg; return 0;
  }
  errno = ENOTTY;
  return -1;
}

ssize_t __real_write(int fd, const void *buf, size_t count);
ssize_t __wrap_write(int fd, const void *buf, size_t count) {
  if (shortWrite)
    return count / 2;
  return __real_write(fd, buf, count);
}

// --- test setup
static char dir[] = "/tmp/piir-lirc-XXXXXX";
static char device[64], remote[64];

static const symbolDefinition SYMBOLS[] = {
  {'H', 9000, 4500}, {'0', 560, 560}, {'1', 560, 1690},
};

static void useDevice(const char *path) {
  FILE *f = fopen(remote, "w");
  fprintf(f, "{\"transmitter\": {\"device\": \"%s\"}}\n", path);
  fclose(f);
  unloadConfig();
  loadConfig(remote);
}

static void reset(void) {
  fakeFeatures = LIRC_CAN_SEND_PULSE | LIRC_CAN_SET_SEND_CARRIER | LIRC_CAN_SET_SEND_DUTY_CYCLE;
  failRequest = 0;
  lastMode = lastCarrier = lastDuty = 0;
  shortWrite = 0;
  int fd = open(device, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  close(fd);
  useDevice(device);
}

static int send(double duty, const char *code) {
  return irSlingGeneric(17, 38000, duty, 3, (symbolDefinition *)SYMBOLS, code);
}

static void test_transmit(void) {
  reset();
  CHECK_EQ_INT(0, send(0.33, "H10"));
  CHECK_EQ_INT(LIRC_MODE_PULSE, lastMode);
  CHECK_EQ_INT(38000, lastCarrier);
  CHECK_EQ_INT(33, lastDuty);

  // The device received the pulses and spaces without the trailing space
  unsigned int values[8];
  FILE *f = fopen(device, "rb");
  size_t n = fread(values, sizeof(values[0]), 8, f);
  fclose(f);
  CHECK_EQ_INT(5, n);
  unsigned int expected[] = {9000, 4500, 560, 1690, 560};
  for (size_t i = 0; i < 5 && i < n; i++)
    CHECK_EQ_INT(expected[i], values[i]);
}

static void test_duty_cycle_limits(void) {
  reset();
  CHECK_EQ_INT(0, send(0.0, "H"));
  CHECK_EQ_INT(1, lastDuty);
  CHECK_EQ_INT(0, send(1.0, "H"));
  CHECK_EQ_INT(99, lastDuty);
}

static void test_optional_features(void) {
  reset();
  fakeFeatures = LIRC_CAN_SEND_PULSE;
  CHECK_EQ_INT(0, send(0.5, "H1"));
  CHECK_EQ_INT(0, lastCarrier);
  CHECK_EQ_INT(0, lastDuty);
}

static void test_cannot_send(void) {
  reset();
  fakeFeatures = LIRC_CAN_SET_SEND_CARRIER;
  CHECK_EQ_INT(1, send(0.5, "H1"));
  reset();
  failRequest = LIRC_GET_FEATURES;
  CHECK_EQ_INT(1, send(0.5, "H1"));
}

static void test_ioctl_failures(void) {
  unsigned long requests[] = {LIRC_SET_SEND_MODE, LIRC_SET_SEND_CARRIER, LIRC_SET_SEND_DUTY_CYCLE};
  for (size_t i = 0; i < 3; i++) {
    reset();
    failRequest = requests[i];
    CHECK_EQ_INT(1, send(0.5, "H1"));
  }
}

static void test_write_failures(void) {
  reset();
  shortWrite = 1;
  CHECK_EQ_INT(1, send(0.5, "H1"));
}

static void test_open_failures(void) {
  reset();
  useDevice("/nonexistent/piir/lirc0");
  CHECK_EQ_INT(1, send(0.5, "H1"));

  if (geteuid() != 0) {   // root may open anything
    reset();
    chmod(device, 0444);
    CHECK_EQ_INT(1, send(0.5, "H1"));
    chmod(device, 0644);
  }

  reset();
  useDevice(dir);   // a directory cannot be opened for writing
  CHECK_EQ_INT(1, send(0.5, "H1"));
}

int main(void) {
  log_set_level(LOG_NOTHING);
  if (!mkdtemp(dir))
    return 2;
  snprintf(device, sizeof(device), "%s/lirc0", dir);
  snprintf(remote, sizeof(remote), "%s/remote.json", dir);

  RUN_TEST(test_transmit);
  RUN_TEST(test_duty_cycle_limits);
  RUN_TEST(test_optional_features);
  RUN_TEST(test_cannot_send);
  RUN_TEST(test_ioctl_failures);
  RUN_TEST(test_write_failures);
  RUN_TEST(test_open_failures);

  unloadConfig();
  unlink(device);
  unlink(remote);
  rmdir(dir);
  return TEST_RESULT();
}
