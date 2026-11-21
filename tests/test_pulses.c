#include <stdlib.h>
#include "unittest.h"
#include "pulses.h"
#include "log.h"

static const symbolDefinition SYMBOLS[] = {
  {'H', 3515, 1744},
  {'0', 436, 436},
  {'1', 436, 1308},
  {'W', 436, 10330},
  {'-', 0, 0},
  {'S', 0, 5000},   // space only
  {'M', 600, 0},    // mark only
};
#define SYMBOL_COUNT (sizeof(SYMBOLS) / sizeof(SYMBOLS[0]))

static unsigned int buffer[MAX_PULSE_VALUES];
static size_t count;

static int build(const char *code) {
  memset(buffer, 0, sizeof(buffer));
  count = 12345;
  return irlingoBuildPulses(SYMBOL_COUNT, SYMBOLS, code, buffer, &count);
}

static void test_single_symbol_drops_trailing_space(void) {
  CHECK_EQ_INT(0, build("H"));
  CHECK_EQ_INT(1, count);
  CHECK_EQ_INT(3515, buffer[0]);
}

static void test_alternating_pulses_and_spaces(void) {
  CHECK_EQ_INT(0, build("H01"));
  CHECK_EQ_INT(5, count);
  CHECK_EQ_INT(3515, buffer[0]);
  CHECK_EQ_INT(1744, buffer[1]);
  CHECK_EQ_INT(436, buffer[2]);
  CHECK_EQ_INT(436, buffer[3]);
  CHECK_EQ_INT(436, buffer[4]);
}

static void test_separators_are_ignored(void) {
  CHECK_EQ_INT(0, build("H-0-1"));
  unsigned int withSeparators[5];
  memcpy(withSeparators, buffer, sizeof(withSeparators));
  size_t countWith = count;
  CHECK_EQ_INT(0, build("H01"));
  CHECK_EQ_INT(count, countWith);
  CHECK(memcmp(buffer, withSeparators, sizeof(withSeparators)) == 0);
}

static void test_adjacent_spaces_merge(void) {
  CHECK_EQ_INT(0, build("0SM"));
  CHECK_EQ_INT(3, count);
  CHECK_EQ_INT(436, buffer[0]);
  CHECK_EQ_INT(436 + 5000, buffer[1]);
  CHECK_EQ_INT(600, buffer[2]);
}

static void test_adjacent_marks_merge(void) {
  CHECK_EQ_INT(0, build("MM0"));
  CHECK_EQ_INT(1, count);
  CHECK_EQ_INT(600 + 600 + 436, buffer[0]);
}

static void test_leading_space_dropped(void) {
  CHECK_EQ_INT(0, build("S0"));
  CHECK_EQ_INT(1, count);
  CHECK_EQ_INT(436, buffer[0]);
}

static void test_nothing_to_transmit(void) {
  CHECK_EQ_INT(1, build(""));
  CHECK_EQ_INT(1, build("---"));
  CHECK_EQ_INT(1, build("SS"));
}

static void test_undefined_symbol(void) {
  CHECK_EQ_INT(1, build("H0X1"));
}

static void test_buffer_limit(void) {
  // Each '0' adds a pulse and a space; the trailing space is dropped
  char code[MAX_PULSE_VALUES / 2 + 2];
  memset(code, '0', MAX_PULSE_VALUES / 2);
  code[MAX_PULSE_VALUES / 2] = 0;
  CHECK_EQ_INT(0, build(code));
  CHECK_EQ_INT(MAX_PULSE_VALUES - 1, count);

  code[MAX_PULSE_VALUES / 2] = '0';
  code[MAX_PULSE_VALUES / 2 + 1] = 0;
  CHECK_EQ_INT(1, build(code));
}

static void test_hvac_panasonic_fits(void) {
  const char *code =
    "H-01000000-00000100-00000111-00100000-00000000-00000000-00000000-01100000-W"
    "H-01000000-00000100-00000111-00100000-00000000-10010000-01010100-00000001-"
    "11110101-10110000-00000000-01110000-00000111-00000000-00000000-10000001-"
    "00000000-00000000-00100111-0";
  CHECK_EQ_INT(0, build(code));
  // 2 headers, the frame gap, 27 bytes of 8 bits and a stop bit, each a
  // pulse and a space, minus the trailing space
  CHECK_EQ_INT((2 + 1 + 27 * 8 + 1) * 2 - 1, count);
}

int main(void) {
  log_set_level(LOG_NOTHING);
  RUN_TEST(test_single_symbol_drops_trailing_space);
  RUN_TEST(test_alternating_pulses_and_spaces);
  RUN_TEST(test_separators_are_ignored);
  RUN_TEST(test_adjacent_spaces_merge);
  RUN_TEST(test_adjacent_marks_merge);
  RUN_TEST(test_leading_space_dropped);
  RUN_TEST(test_nothing_to_transmit);
  RUN_TEST(test_undefined_symbol);
  RUN_TEST(test_buffer_limit);
  RUN_TEST(test_hvac_panasonic_fits);
  return TEST_RESULT();
}
