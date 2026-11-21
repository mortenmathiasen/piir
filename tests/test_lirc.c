// The lirc backend's transmission errors; turning codes into pulses is
// tested in test_pulses.c
#include <stdlib.h>
#include "unittest.h"
#include "irgeneric.h"
#include "config.h"
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

static void test_transmit_errors(void) {
  // Code longer than MAX_COMMAND_SIZE
  char longCode[MAX_COMMAND_SIZE + 2];
  memset(longCode, '0', sizeof(longCode) - 1);
  longCode[sizeof(longCode) - 1] = 0;
  CHECK_EQ_INT(1, irSlingGeneric(17, 38000, 0.5, SYMBOL_COUNT, (symbolDefinition *)SYMBOLS, longCode));

  CHECK_EQ_INT(1, irSlingGeneric(17, 38000, 0.5, SYMBOL_COUNT, (symbolDefinition *)SYMBOLS, "H0X"));

  // Device taken from the configuration; this one cannot be opened
  loadConfig(FIXTURE_DIR "/missing_device.json");
  CHECK(isConfigLoaded());
  CHECK_EQ_INT(1, irSlingGeneric(17, 38000, 0.5, SYMBOL_COUNT, (symbolDefinition *)SYMBOLS, "H01"));

  // A device that is not a LIRC device fails the feature check
  loadConfig(FIXTURE_DIR "/not_lirc_device.json");
  CHECK(isConfigLoaded());
  CHECK_EQ_INT(1, irSlingGeneric(17, 38000, 0.5, SYMBOL_COUNT, (symbolDefinition *)SYMBOLS, "H01"));
  unloadConfig();
}

int main(void) {
  log_set_level(LOG_NOTHING);
  RUN_TEST(test_transmit_errors);
  return TEST_RESULT();
}
