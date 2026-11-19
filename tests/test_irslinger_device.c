// The pigpio backend. pigpio is replaced at link time (-Wl,--wrap) with a
// simulation recording the wave it is given.
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include "unittest.h"
#include "irslinger.h"
#include "log.h"

static int initResult, addResult, createResult, sendResult;
static int addedPulses, deleted, terminated, busyPolls, mode;
static int useAddResult;

int __wrap_gpioCfgDMAchannels(unsigned p, unsigned s) { (void)p; (void)s; return 0; }
int __wrap_gpioInitialise(void) { return initResult; }
int __wrap_gpioSetMode(unsigned gpio, unsigned m) { (void)gpio; mode = m; return 0; }
int __wrap_gpioWaveClear(void) { return 0; }
int __wrap_gpioWaveAddGeneric(unsigned n, gpioPulse_t *pulses) {
  (void)pulses;
  addedPulses = n;
  return useAddResult ? addResult : (int)n;
}
int __wrap_gpioWaveCreate(void) { return createResult; }
int __wrap_gpioWaveTxSend(unsigned id, unsigned m) { (void)id; (void)m; return sendResult; }
int __wrap_gpioWaveTxBusy(void) { return busyPolls-- > 0; }
int __wrap_gpioWaveDelete(unsigned id) { (void)id; deleted++; return 0; }
void __wrap_gpioTerminate(void) { terminated++; }
double __wrap_time_sleep(double seconds) { (void)seconds; return 0; }

static symbolDefinition SYMBOLS[] = {{'0', 100, 200}};

static void reset(void) {
  initResult = 0;
  useAddResult = 0;
  createResult = 3;
  sendResult = 42;
  addedPulses = deleted = terminated = mode = 0;
  busyPolls = 2;
}

static void test_transmit(void) {
  reset();
  CHECK_EQ_INT(0, irSlingGeneric(17, 10000, 0.5, 1, SYMBOLS, "00"));
  CHECK_EQ_INT(PI_OUTPUT, mode);
  CHECK_EQ_INT(4, addedPulses);   // on, off+space, on, off+space
  CHECK_EQ_INT(1, deleted);
  CHECK_EQ_INT(1, terminated);
}

static void test_trace_logging(void) {
  reset();
  log_set_level(LOG_TRACE);
  log_set_quiet(true);
  CHECK_EQ_INT(0, irSlingGeneric(17, 10000, 0.5, 1, SYMBOLS, "0"));
  log_set_quiet(false);
  log_set_level(LOG_NOTHING);
}

static void test_send_errors(void) {
  reset();
  sendResult = PI_BAD_WAVE_ID;
  CHECK_EQ_INT(0, irSlingGeneric(17, 10000, 0.5, 1, SYMBOLS, "0"));
  reset();
  sendResult = PI_BAD_WAVE_MODE;
  CHECK_EQ_INT(0, irSlingGeneric(17, 10000, 0.5, 1, SYMBOLS, "0"));
  reset();
  createResult = -1;
  CHECK_EQ_INT(0, irSlingGeneric(17, 10000, 0.5, 1, SYMBOLS, "0"));
  CHECK_EQ_INT(0, deleted);
}

static void test_initialise_fails(void) {
  reset();
  initResult = -1;
  CHECK_EQ_INT(1, irSlingGeneric(17, 10000, 0.5, 1, SYMBOLS, "0"));
}

// These errors abort(); run them in a child process
static int abortsInChild(void (*fn)(void)) {
  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    fn();
    _exit(0);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

static void tooManyPulses(void) {
  reset();
  useAddResult = 1;
  addResult = PI_TOO_MANY_PULSES;
  irSlingGeneric(17, 10000, 0.5, 1, SYMBOLS, "0");
}

static void wrongPulseCount(void) {
  reset();
  useAddResult = 1;
  addResult = 1;
  irSlingGeneric(17, 10000, 0.5, 1, SYMBOLS, "0");
}

static void invalidPulse(void) {
  gpioPulse_t pulses[2];
  unsigned int count = 0;
  addPulse(0, 0, 10, pulses, &count);
}

static void test_aborts(void) {
  CHECK(abortsInChild(tooManyPulses));
  CHECK(abortsInChild(wrongPulseCount));
  CHECK(abortsInChild(invalidPulse));
}

int main(void) {
  log_set_level(LOG_NOTHING);
  RUN_TEST(test_transmit);
  RUN_TEST(test_trace_logging);
  RUN_TEST(test_send_errors);
  RUN_TEST(test_initialise_fails);
  RUN_TEST(test_aborts);
  return TEST_RESULT();
}
