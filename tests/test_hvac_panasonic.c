#include <stdlib.h>
#include "unittest.h"
#include "hvac_panasonic_plugin.h"
#include "plugins.h"

// Test vector "No 1" from conf/remotes/hvac_panasonic.json
static const char *VALID =
  "H-01000000-00000100-00000111-00100000-00000000-00000000-00000000-01100000-W"
  "H-01000000-00000100-00000111-00100000-00000000-10010000-01010100-00000001-"
  "11110101-10110000-00000000-01110000-00000111-00000000-00000000-10000001-"
  "00000000-00000000-00100111-0";

static char *with_checksum(const char *checksum) {
  char *code = strdup(VALID);
  char *last = strrchr(code, '-') - 8; // checksum byte before the stop bit
  memcpy(last, checksum, 8);
  return code;
}

static void test_restores_checksum(void) {
  char *code = with_checksum("00000000");
  hvac_panasonic_plugin(code);
  CHECK_EQ_STR(VALID, code);
  free(code);

  code = with_checksum("11111111");
  hvac_panasonic_plugin(code);
  CHECK_EQ_STR(VALID, code);
  free(code);
}

static void test_idempotent(void) {
  char *code = strdup(VALID);
  hvac_panasonic_plugin(code);
  hvac_panasonic_plugin(code);
  CHECK_EQ_STR(VALID, code);
  free(code);
}

static void test_checksum_follows_payload(void) {
  char *code = strdup(VALID);
  // Flip the lowest bit of the first frame 2 data byte (bits are sent LSB first)
  char *byte = code + strlen("H-01000000-00000100-00000111-00100000-00000000-00000000-00000000-01100000-W") +
               strlen("H-");
  byte[0] = byte[0] == '0' ? '1' : '0';
  hvac_panasonic_plugin(code);
  CHECK(strcmp(VALID, code) != 0);
  // Only the flipped bit and the checksum may differ
  size_t len = strlen(VALID);
  size_t checksumStart = len - strlen("00100111-0");
  for (size_t i = 0; i < len; i++)
    if (code + i != byte && i < checksumStart)
      CHECK(code[i] == VALID[i]);
  free(code);
}

static void test_plugin_dispatch(void) {
  char *code = with_checksum("00000000");
  applyRemotePlugins("/somewhere/else/tv_sony.json", code);
  CHECK(strcmp(VALID, code) != 0);
  applyRemotePlugins("/usr/share/piir/conf/remotes/hvac_panasonic.json", code);
  CHECK_EQ_STR(VALID, code);
  free(code);
}

int main(void) {
  RUN_TEST(test_restores_checksum);
  RUN_TEST(test_idempotent);
  RUN_TEST(test_checksum_follows_payload);
  RUN_TEST(test_plugin_dispatch);
  return TEST_RESULT();
}
