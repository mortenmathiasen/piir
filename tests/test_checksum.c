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

static void test_crc(void) {
  // Check values of the CRC catalogue for the bytes of "123456789"
  // CRC-8/SMBUS
  CHECK_CHECKSUM("[{\"type\": \"crc\", \"poly\": 7, \"from\": 0, \"to\": 8, \"at\": 9, \"bitorder\": \"msb\"}]",
		 "00110001-00110010-00110011-00110100-00110101-00110110-00110111-00111000-00111001-00000000",
		 "00110001-00110010-00110011-00110100-00110101-00110110-00110111-00111000-00111001-11110100");
  // CRC-8/MAXIM-DOW, sent lowest bit first
  CHECK_CHECKSUM("[{\"type\": \"crc\", \"poly\": 49, \"reflect\": true, \"from\": 0, \"to\": 8, \"at\": 9}]",
		 "10001100-01001100-11001100-00101100-10101100-01101100-11101100-00011100-10011100-00000000",
		 "10001100-01001100-11001100-00101100-10101100-01101100-11101100-00011100-10011100-10000101");
  // CRC-16/IBM-3740, the high byte first
  CHECK_CHECKSUM("[{\"type\": \"crc\", \"poly\": 4129, \"init\": 65535, \"from\": 0, \"to\": 8, \"at\": [9, 10], \"bitorder\": \"msb\"}]",
		 "00110001-00110010-00110011-00110100-00110101-00110110-00110111-00111000-00111001-00000000-00000000",
		 "00110001-00110010-00110011-00110100-00110101-00110110-00110111-00111000-00111001-00101001-10110001");
  // CRC-16/MODBUS, the low byte first
  CHECK_CHECKSUM("[{\"type\": \"crc\", \"poly\": 32773, \"init\": 65535, \"reflect\": true, \"from\": 0, \"to\": 8, \"at\": [9, 10]}]",
		 "10001100-01001100-11001100-00101100-10101100-01101100-11101100-00011100-10011100-00000000-00000000",
		 "10001100-01001100-11001100-00101100-10101100-01101100-11101100-00011100-10011100-11101100-11010010");
  // CRC-8/SAE-J1850
  CHECK_CHECKSUM("[{\"type\": \"crc\", \"poly\": 29, \"init\": 255, \"xorout\": 255, \"from\": 0, \"to\": 8, \"at\": 9, \"bitorder\": \"msb\"}]",
		 "00110001-00110010-00110011-00110100-00110101-00110110-00110111-00111000-00111001-00000000",
		 "00110001-00110010-00110011-00110100-00110101-00110110-00110111-00111000-00111001-01001011");
}

static void test_popcount(void) {
  // 0x0F and 0x03 have 6 bits set
  CHECK_CHECKSUM("[{\"type\": \"popcount\", \"from\": 0, \"to\": 1, \"at\": 2}]",
		 "11110000-11000000-11111111",
		 "11110000-11000000-01100000");
  // Samsung FJM: 28 - count, as 0 - (4 + count) modulo 16, into a nibble:
  // 5 bits set, 28 - 5 = 23, 7 modulo 16
  CHECK_CHECKSUM("[{\"type\": \"popcount\", \"unit\": \"nibble\", \"from\": 2, \"to\": 5, \"at\": 1, \"init\": 4, \"negate\": true}]",
		 "0000-0000-1111-1000-0000-0000",
		 "0000-1110-1111-1000-0000-0000");
}

static void test_result_units(void) {
  // Samsung AQV: ~count of the other nibbles, the low nibble into nibble 1
  // and the high one into nibble 2: 5 bits set, ~5 = 0xFA
  CHECK_CHECKSUM("[{\"type\": \"popcount\", \"unit\": \"nibble\", \"data\": [0, 3], \"at\": [1, 2], \"invert\": true}]",
		 "1110-0000-0000-1100",
		 "1110-0101-1111-1100");
  // With "msb" the first unit holds the high bits: 0x01 + 0xFF = 0x0100
  CHECK_CHECKSUM("[{\"type\": \"sum\", \"from\": 0, \"to\": 1, \"at\": [2, 3], \"bitorder\": \"msb\"}]",
		 "00000001-11111111-11111111-11111111",
		 "00000001-11111111-00000001-00000000");
  // Written in the order listed
  CHECK_CHECKSUM("[{\"type\": \"sum\", \"from\": 0, \"to\": 1, \"at\": [3, 2], \"bitorder\": \"msb\"}]",
		 "00000001-11111111-11111111-11111111",
		 "00000001-11111111-00000000-00000001");
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
  CHECK_INVALID("[{\"type\": \"crc\", \"poly\": 256, \"from\": 0, \"to\": 1, \"at\": 2}]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"poly\": 7, \"from\": 0, \"to\": 1, \"at\": 2}]", code);
  CHECK_INVALID("[{\"type\": \"xor\", \"reflect\": true, \"from\": 0, \"to\": 1, \"at\": 2}]", code);
  CHECK_INVALID("[{\"type\": \"crc\", \"poly\": 7, \"reflect\": 1, \"from\": 0, \"to\": 1, \"at\": 2}]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"from\": 0, \"to\": 1, \"at\": 2, \"xorout\": 256}]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"from\": 0, \"to\": 0, \"at\": [1, 1]}]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"from\": 0, \"to\": 1, \"at\": [2, 1]}]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"from\": 0, \"to\": 0, \"at\": [1, 2, 3]}]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"from\": 0, \"to\": 0, \"at\": []}]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"from\": 0, \"to\": 0, \"at\": [1, \"2\"]}]", code);
  CHECK_INVALID("[{\"type\": \"sum\", \"unit\": \"nibble\", \"from\": 0, \"to\": 0, \"at\": [1, 2, 3, 4, 5, 6, 7, 8, 9]}]", code);
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
  RUN_TEST(test_crc);
  RUN_TEST(test_popcount);
  RUN_TEST(test_result_units);
  RUN_TEST(test_invalid);
  RUN_TEST(test_panasonic);
  return TEST_RESULT();
}
