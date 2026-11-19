// Pulse generation for the libgpiod backend
#include <stdlib.h>
#include "unittest.h"
#include "transmitter.h"
#include "log.h"

static gpioPulse_t pulses[MAX_PULSES];
static unsigned int count;

static unsigned long total(void) {
  unsigned long sum = 0;
  for (unsigned int i = 0; i < count; i++)
    sum += pulses[i].usDelay;
  return sum;
}

static void test_add_pulse_merges_same_level(void) {
  count = 0;
  CHECK_EQ_INT(0, addPulse(1, 100, pulses, &count));
  CHECK_EQ_INT(0, addPulse(1, 50, pulses, &count));
  CHECK_EQ_INT(0, addPulse(0, 30, pulses, &count));
  CHECK_EQ_INT(2, count);
  CHECK(pulses[0].high);
  CHECK_EQ_INT(150, pulses[0].usDelay);
  CHECK(!pulses[1].high);
  CHECK_EQ_INT(30, pulses[1].usDelay);
}

static void test_add_pulse_ignores_zero_duration(void) {
  count = 0;
  CHECK_EQ_INT(0, addPulse(1, 0, pulses, &count));
  CHECK_EQ_INT(0, count);
}

static void test_add_pulse_limit(void) {
  count = 0;
  for (unsigned int i = 0; i < MAX_PULSES; i++)
    CHECK_EQ_INT(0, addPulse(i % 2, 10, pulses, &count));
  CHECK_EQ_INT(MAX_PULSES, count);
  // Merging into the last pulse still works, a new pulse does not
  CHECK_EQ_INT(0, addPulse((MAX_PULSES - 1) % 2, 10, pulses, &count));
  CHECK_EQ_INT(1, addPulse(MAX_PULSES % 2, 10, pulses, &count));
}

static void test_mark_carrier(void) {
  count = 0;
  // 38 kHz for 436 us: about 16.6 cycles rounds to 17 cycles of 26.3 us
  CHECK_EQ_INT(0, addMark(38000, 0.5, 436, pulses, &count));
  CHECK_EQ_INT(34, count);
  CHECK(pulses[0].high);
  CHECK(!pulses[1].high);
  CHECK(total() >= 440 && total() <= 450);
  for (unsigned int i = 0; i < count; i++)
    CHECK(pulses[i].usDelay == 13 || pulses[i].usDelay == 14);
}

static void test_mark_duty_cycle(void) {
  count = 0;
  CHECK_EQ_INT(0, addMark(10000, 0.25, 1000, pulses, &count));
  CHECK_EQ_INT(20, count);
  for (unsigned int i = 0; i < count; i += 2) {
    CHECK_EQ_INT(25, pulses[i].usDelay);
    CHECK_EQ_INT(75, pulses[i + 1].usDelay);
  }
}

static void test_space(void) {
  count = 0;
  CHECK_EQ_INT(0, addSpace(1744, pulses, &count));
  CHECK_EQ_INT(0, addSpace(256, pulses, &count));
  CHECK_EQ_INT(1, count);
  CHECK(!pulses[0].high);
  CHECK_EQ_INT(2000, pulses[0].usDelay);
}

static void test_transmit_errors(void) {
  symbolDefinition symbols[] = {{'0', 436, 436}};
  CHECK_EQ_INT(1, irSlingGeneric(32, 38000, 0.5, 1, symbols, "0"));
  CHECK_EQ_INT(1, irSlingGeneric(17, 38000, 0.5, 1, symbols, "01"));

  char longCode[MAX_COMMAND_SIZE + 2];
  memset(longCode, '0', sizeof(longCode) - 1);
  longCode[sizeof(longCode) - 1] = 0;
  CHECK_EQ_INT(1, irSlingGeneric(17, 38000, 0.5, 1, symbols, longCode));
}

int main(void) {
  log_set_level(LOG_NOTHING);
  RUN_TEST(test_add_pulse_merges_same_level);
  RUN_TEST(test_add_pulse_ignores_zero_duration);
  RUN_TEST(test_add_pulse_limit);
  RUN_TEST(test_mark_carrier);
  RUN_TEST(test_mark_duty_cycle);
  RUN_TEST(test_space);
  RUN_TEST(test_transmit_errors);
  return TEST_RESULT();
}
