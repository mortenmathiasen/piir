#include <stdlib.h>
#include "unittest.h"
#include "checksum.h"
#include "parson.h"
#include "log.h"

// Apply the checksums in json (an array) to a copy of code, return the
// result of applyChecksums and the code in *out (to be freed)
static int apply(const char *json, const char *code, char **out) {
  JSON_Value *value = json_parse_string(json);
  CHECK(value != NULL);
  *out = strdup(code);
  int result = applyChecksums(json_value_get_array(value), *out);
  json_value_free(value);
  return result;
}

#define CHECK_CHECKSUM(json, code, expected) do { \
    char *out_; \
    CHECK_EQ_INT(0, apply(json, code, &out_)); \
    CHECK_EQ_STR(expected, out_); \
    free(out_); \
  } while (0)

#define CHECK_INVALID(json, code) do { \
    char *out_; \
    CHECK_EQ_INT(1, apply(json, code, &out_)); \
    CHECK_EQ_STR(code, out_); \
    free(out_); \
  } while (0)

static void test_sum_lsb_first(void) {
  // 0x01 + 0x02 = 0x03, bits sent lowest first
  CHECK_CHECKSUM("[{\"type\": \"sum\", \"from\": 0, \"to\": 1, \"at\": 2}]",
		 "10000000-01000000-00000000",
		 "10000000-01000000-11000000");
}

static void test_sum_msb_first(void) {
  // 0x01 + 0x03 = 0x04
  CHECK_CHECKSUM("[{\"type\": \"sum\", \"from\": 0, \"to\": 1, \"at\": 2, \"bitorder\": \"msb\"}]",
		 "00000001-00000011-11111111",
		 "00000001-00000011-00000100");
}

static void test_xor(void) {
  // 0x0F ^ 0x05 = 0x0A
  CHECK_CHECKSUM("[{\"type\": \"xor\", \"from\": 0, \"to\": 1, \"at\": 2}]",
		 "11110000-10100000-00000000",
		 "11110000-10100000-01010000");
}

static void test_init_and_overflow(void) {
  // 1 + 0xFF + 0x02 = 0x102, modulo 256 0x02
  CHECK_CHECKSUM("[{\"type\": \"sum\", \"from\": 0, \"to\": 1, \"at\": 2, \"init\": 1, \"bitorder\": \"msb\"}]",
		 "11111111-00000010-00000000",
		 "11111111-00000010-00000010");
}

static void test_invert_and_negate(void) {
  // ~(0x01 + 0x02) = 0xFC
  CHECK_CHECKSUM("[{\"type\": \"sum\", \"from\": 0, \"to\": 1, \"at\": 2, \"invert\": true, \"bitorder\": \"msb\"}]",
		 "00000001-00000010-00000000",
		 "00000001-00000010-11111100");
  // 0 - (0x01 + 0x02) = 0xFD
  CHECK_CHECKSUM("[{\"type\": \"sum\", \"from\": 0, \"to\": 1, \"at\": 2, \"negate\": true, \"bitorder\": \"msb\"}]",
		 "00000001-00000010-00000000",
		 "00000001-00000010-11111101");
  // Both: 0 - ~0x03 = 0 - 0xFC = 0x04
  CHECK_CHECKSUM("[{\"type\": \"sum\", \"from\": 0, \"to\": 1, \"at\": 2, \"invert\": true, \"negate\": true, \"bitorder\": \"msb\"}]",
		 "00000001-00000010-00000000",
		 "00000001-00000010-00000100");
}

static void test_checksum_before_data(void) {
  CHECK_CHECKSUM("[{\"type\": \"sum\", \"from\": 1, \"to\": 2, \"at\": 0, \"bitorder\": \"msb\"}]",
		 "00000000-00000001-00000010",
		 "00000011-00000001-00000010");
}

static void test_only_bit_symbols_count(void) {
  // Bytes need not follow the separators, other symbols and a trailing
  // partial byte are ignored
  CHECK_CHECKSUM("[{\"type\": \"sum\", \"from\": 0, \"to\": 1, \"at\": 2}]",
		 "S1000-0000-0100-0000-0000-0000-1",
		 "S1000-0000-0100-0000-1100-0000-1");
  // Other bit symbols
  CHECK_CHECKSUM("[{\"type\": \"sum\", \"from\": 0, \"to\": 1, \"at\": 2, \"bits\": \"AB\"}]",
		 "SBAAAAAAA-BBAAAAAA-BBBBBBBB-0",
		 "SBAAAAAAA-BBAAAAAA-AABAAAAA-0");
}

static void test_applied_in_order(void) {
  // The second checksum covers the byte the first one wrote
  CHECK_CHECKSUM("[{\"type\": \"sum\", \"from\": 0, \"to\": 0, \"at\": 1, \"bitorder\": \"msb\"},"
		 " {\"type\": \"sum\", \"from\": 0, \"to\": 1, \"at\": 2, \"bitorder\": \"msb\"}]",
		 "00000001-00000000-00000000",
		 "00000001-00000001-00000010");
}

static void test_no_checksums(void) {
  CHECK_CHECKSUM("[]", "10101010", "10101010");
  char code[] = "10101010";
  CHECK_EQ_INT(0, applyChecksums(NULL, code));
  CHECK_EQ_STR("10101010", code);
}

static void test_nibbles(void) {
  // 0x1 + 0x2 + 0x3 = 0x6, nibbles numbered in the order they are sent
  CHECK_CHECKSUM("[{\"type\": \"sum\", \"unit\": \"nibble\", \"from\": 0, \"to\": 2, \"at\": 3, \"bitorder\": \"msb\"}]",
		 "0001-0010-0011-1111",
		 "0001-0010-0011-0110");
  // A byte sent lowest bit first holds its low nibble first: 10000100 is
  // 0x21, nibble 0 is 0x1 and nibble 1 is 0x2
  CHECK_CHECKSUM("[{\"type\": \"sum\", \"unit\": \"nibble\", \"from\": 0, \"to\": 1, \"at\": 3}]",
		 "10000100-10100000",
		 "10000100-10101100");
  // Modulo 16 with a start value: 3 + 0xF + 0xF = 0x21
  CHECK_CHECKSUM("[{\"type\": \"sum\", \"unit\": \"nibble\", \"from\": 0, \"to\": 1, \"at\": 2, \"init\": 3, \"bitorder\": \"msb\"}]",
		 "1111-1111-0000",
		 "1111-1111-0001");
  // ~(0x1 + 0x2) = 0xC, 0 - 0x3 = 0xD, 0x5 ^ 0x6 = 0x3
  CHECK_CHECKSUM("[{\"type\": \"sum\", \"unit\": \"nibble\", \"from\": 0, \"to\": 1, \"at\": 2, \"invert\": true, \"bitorder\": \"msb\"}]",
		 "0001-0010-0000",
		 "0001-0010-1100");
  CHECK_CHECKSUM("[{\"type\": \"sum\", \"unit\": \"nibble\", \"from\": 0, \"to\": 1, \"at\": 2, \"negate\": true, \"bitorder\": \"msb\"}]",
		 "0001-0010-0000",
		 "0001-0010-1101");
  CHECK_CHECKSUM("[{\"type\": \"xor\", \"unit\": \"nibble\", \"from\": 0, \"to\": 1, \"at\": 2, \"bitorder\": \"msb\"}]",
		 "0101-0110-0000",
		 "0101-0110-0011");
  // Explicitly bytes
  CHECK_CHECKSUM("[{\"type\": \"sum\", \"unit\": \"byte\", \"from\": 0, \"to\": 1, \"at\": 2}]",
		 "10000000-01000000-00000000",
		 "10000000-01000000-11000000");
}

static void test_data_list(void) {
  // Only the listed nibbles: 0x1 + 0x2, skipping the 0xF ones
  CHECK_CHECKSUM("[{\"type\": \"sum\", \"unit\": \"nibble\", \"data\": [0, 2], \"at\": 4, \"bitorder\": \"msb\"}]",
		 "0001-1111-0010-1111-0000",
		 "0001-1111-0010-1111-0011");
  // Bytes in any order, the checksum between them
  CHECK_CHECKSUM("[{\"type\": \"xor\", \"data\": [2, 0], \"at\": 1, \"bitorder\": \"msb\"}]",
		 "00000011-00000000-00000101",
		 "00000011-00000110-00000101");
}

static void test_restart(void) {
  // The first frame has 6 bits. With restart byte 0 is 00000011; without,
  // byte 0 is 10101000 and byte 1 the bits 8-15, across the separator
  CHECK_CHECKSUM("[{\"type\": \"sum\", \"from\": 0, \"to\": 0, \"at\": 1, \"restart\": \"W\", \"bitorder\": \"msb\"}]",
		 "H101010W00000011-00000000",
		 "H101010W00000011-00000011");
  CHECK_CHECKSUM("[{\"type\": \"sum\", \"from\": 0, \"to\": 0, \"at\": 1, \"bitorder\": \"msb\"}]",
		 "H101010W00000011-00000000",
		 "H101010W00101010-00000000");
  // A restart symbol after a complete unit changes nothing
  CHECK_CHECKSUM("[{\"type\": \"sum\", \"from\": 0, \"to\": 0, \"at\": 1, \"restart\": \"W\", \"bitorder\": \"msb\"}]",
		 "00000011W00000000",
		 "00000011W00000011");
}

static void test_invalid(void) {
  const char *code = "00000001-00000010-00000000";
  CHECK_INVALID("[{\"type\": \"crc\", \"from\": 0, \"to\": 1, \"at\": 2}]", code);
  CHECK_INVALID("[{\"from\": 0, \"to\": 1, \"at\": 2}]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"from\": 0, \"to\": 1}]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"from\": 1, \"to\": 0, \"at\": 2}]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"from\": 0, \"to\": 2, \"at\": 1}]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"from\": 0, \"to\": 1, \"at\": 3}]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"from\": 0, \"to\": 3, \"at\": 4}]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"from\": -1, \"to\": 1, \"at\": 2}]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"from\": 0.5, \"to\": 1, \"at\": 2}]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"from\": \"0\", \"to\": 1, \"at\": 2}]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"from\": 0, \"to\": 1, \"at\": 2, \"init\": 256}]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"from\": 0, \"to\": 1, \"at\": 2, \"invert\": 1}]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"from\": 0, \"to\": 1, \"at\": 2, \"bitorder\": \"big\"}]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"from\": 0, \"to\": 1, \"at\": 2, \"bits\": \"0\"}]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"from\": 0, \"to\": 1, \"at\": 2, \"bits\": \"00\"}]", code);
  CHECK_INVALID("[1]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"from\": 0, \"to\": 1, \"at\": 2, \"restart\": \"-0\"}]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"from\": 0, \"to\": 1, \"at\": 2, \"restart\": 1}]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"unit\": \"word\", \"from\": 0, \"to\": 1, \"at\": 2}]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"unit\": \"nibble\", \"from\": 0, \"to\": 1, \"at\": 2, \"init\": 16}]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"unit\": \"nibble\", \"from\": 0, \"to\": 1, \"at\": 6}]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"data\": [0, 1], \"from\": 0, \"at\": 2}]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"data\": [], \"at\": 2}]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"data\": 0, \"at\": 2}]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"data\": [0, -1], \"at\": 2}]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"data\": [0, \"1\"], \"at\": 2}]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"data\": [0, 2], \"at\": 2}]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"data\": [0, 3], \"at\": 2}]", code);
}

// Test vector "No 1" from conf/remotes/panasonic/hvac_panasonic.json
static const char *PANASONIC =
  "H-01000000-00000100-00000111-00100000-00000000-00000000-00000000-01100000-W"
  "H-01000000-00000100-00000111-00100000-00000000-10010000-01010100-00000001-"
  "11110101-10110000-00000000-01110000-00000111-00000000-00000000-10000001-"
  "00000000-00000000-00100111-0";

static void test_panasonic(void) {
  // The checksum of the remote file computes what the hvac_panasonic plugin does
  char *code = strdup(PANASONIC);
  char *checksum = strrchr(code, '-') - 8;
  memcpy(checksum, "11111111", 8);
  char *out;
  CHECK_EQ_INT(0, apply("[{\"type\": \"sum\", \"from\": 8, \"to\": 25, \"at\": 26}]", code, &out));
  CHECK_EQ_STR(PANASONIC, out);
  free(out);
  free(code);
}

int main(void) {
  log_set_level(LOG_NOTHING);
  RUN_TEST(test_sum_lsb_first);
  RUN_TEST(test_sum_msb_first);
  RUN_TEST(test_xor);
  RUN_TEST(test_init_and_overflow);
  RUN_TEST(test_invert_and_negate);
  RUN_TEST(test_checksum_before_data);
  RUN_TEST(test_only_bit_symbols_count);
  RUN_TEST(test_applied_in_order);
  RUN_TEST(test_no_checksums);
  RUN_TEST(test_nibbles);
  RUN_TEST(test_data_list);
  RUN_TEST(test_restart);
  RUN_TEST(test_invalid);
  RUN_TEST(test_panasonic);
  return TEST_RESULT();
}
