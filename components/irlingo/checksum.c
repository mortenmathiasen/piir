#include <stdlib.h>
#include <string.h>
#include "checksum.h"
#include "config.h"
#include "pulses.h"
#include "log.h"

// Read an optional whole number between 0 and max. Returns 0 when it is
// valid or missing (then *value is left as it is).
static int readNumber(const JSON_Object *spec, const char *name, int required,
		      long long max, long long *value)
{
  JSON_Value *v = json_object_get_value(spec, name);
  if (!v) {
    if (required)
      log_fatal("Checksum needs \"%s\"", name);
    return required;
  }
  double d = json_value_get_number(v);
  if (json_value_get_type(v) != JSONNumber || d < 0 || d > max || d != (double)(long long)d) {
    log_fatal("Checksum \"%s\" must be a whole number from 0 to %lld", name, max);
    return 1;
  }
  *value = (long long)d;
  return 0;
}

static int readFlag(const JSON_Object *spec, const char *name, int *value)
{
  JSON_Value *v = json_object_get_value(spec, name);
  if (!v)
    return 0;
  if (json_value_get_type(v) != JSONBoolean) {
    log_fatal("Checksum \"%s\" must be true or false", name);
    return 1;
  }
  *value = json_value_get_boolean(v);
  return 0;
}

// Read unit number index of width bits
static unsigned readUnit(const char *code, const size_t *bitPos, long index, int width,
			 int msbFirst, char one)
{
  unsigned value = 0;
  for (int i = 0; i < width; i++)
    if (code[bitPos[index * width + i]] == one)
      value |= 1u << (msbFirst ? width - 1 - i : i);
  return value;
}

static void writeUnit(char *code, const size_t *bitPos, long index, int width,
		      int msbFirst, char zero, char one, unsigned value)
{
  for (int i = 0; i < width; i++)
    code[bitPos[index * width + i]] = value & (1u << (msbFirst ? width - 1 - i : i)) ? one : zero;
}

// Read a list of unit numbers into units (to be freed), its length in *count
static int readUnitList(const JSON_Array *array, const char *name, long **units, size_t *count)
{
  *count = json_array_get_count(array);
  if (!array || !*count) {
    log_fatal("Checksum \"%s\" must be a list of unit numbers", name);
    return 1;
  }
  *units = malloc(*count * sizeof(long));
  for (size_t i = 0; *units && i < *count; i++) {
    double d = json_array_get_number(array, i);
    if (json_value_get_type(json_array_get_value(array, i)) != JSONNumber ||
	d < 0 || d > MAX_COMMAND_SIZE || d != (double)(long)d) {
      log_fatal("Checksum \"%s\" must hold whole numbers from 0 to %d", name, MAX_COMMAND_SIZE);
      free(*units);
      *units = NULL;
      return 1;
    }
    (*units)[i] = (long)d;
  }
  return *units == NULL;
}

// The units a checksum covers, from "data" or from "from" to "to". Returns
// the list (to be freed) and its length in *count, or NULL when invalid.
static long *readCoveredUnits(const JSON_Object *spec, size_t *count)
{
  JSON_Value *data = json_object_get_value(spec, "data");
  int hasRange = json_object_get_value(spec, "from") || json_object_get_value(spec, "to");
  if (data && hasRange) {
    log_fatal("Checksum needs either \"data\" or \"from\" and \"to\", not both");
    return NULL;
  }

  long *units = NULL;
  if (data) {
    readUnitList(json_value_get_array(data), "data", &units, count);
    return units;
  }

  long long from = 0, to = 0;
  if (readNumber(spec, "from", 1, MAX_COMMAND_SIZE, &from) ||
      readNumber(spec, "to", 1, MAX_COMMAND_SIZE, &to))
    return NULL;
  if (from > to) {
    log_fatal("Checksum \"from\" (%lld) is after \"to\" (%lld)", from, to);
    return NULL;
  }
  *count = to - from + 1;
  units = malloc(*count * sizeof(long));
  for (size_t i = 0; units && i < *count; i++)
    units[i] = from + i;
  return units;
}

// The units the result is written into, "at": a unit number or a list
static long *readResultUnits(const JSON_Object *spec, size_t *count)
{
  JSON_Value *v = json_object_get_value(spec, "at");
  long *units = NULL;
  if (v && json_value_get_type(v) == JSONArray) {
    readUnitList(json_value_get_array(v), "at", &units, count);
    return units;
  }
  long long at = 0;
  if (readNumber(spec, "at", 1, MAX_COMMAND_SIZE, &at))
    return NULL;
  units = malloc(sizeof(long));
  if (units)
    units[0] = at;
  *count = 1;
  return units;
}

static unsigned reflect(unsigned value, int width)
{
  unsigned result = 0;
  for (int i = 0; i < width; i++)
    if (value & (1u << i))
      result |= 1u << (width - 1 - i);
  return result;
}

// Feed the unit value u of unitWidth bits to a CRC register of crcWidth bits
static unsigned crcUnit(unsigned crc, unsigned u, int unitWidth, unsigned poly, int crcWidth,
			int reflected, unsigned mask)
{
  for (int i = 0; i < unitWidth; i++) {
    if (reflected) {
      unsigned bit = (crc ^ (u >> i)) & 1;
      crc >>= 1;
      if (bit)
	crc ^= reflect(poly, crcWidth);
    } else {
      unsigned bit = ((crc >> (crcWidth - 1)) ^ (u >> (unitWidth - 1 - i))) & 1;
      crc = (crc << 1) & mask;
      if (bit)
	crc ^= poly;
    }
  }
  return crc;
}

static int bitCount(unsigned value)
{
  int count = 0;
  for (; value; value &= value - 1)
    count++;
  return count;
}

enum checksumType { SUM, XOR, POPCOUNT, CRC };

static int applyChecksum(const JSON_Object *spec, char *code, size_t *bitPos)
{
  static const char *typeNames[] = { "sum", "xor", "popcount", "crc" };
  const char *type = json_object_get_string(spec, "type");
  int kind = -1;
  for (int i = 0; type && i < 4; i++)
    if (strcmp(type, typeNames[i]) == 0)
      kind = i;
  if (kind < 0) {
    log_fatal("Checksum \"type\" must be \"sum\", \"xor\", \"popcount\" or \"crc\"");
    return 1;
  }

  const char *unit = json_object_get_string(spec, "unit");
  if (json_object_get_value(spec, "unit") &&
      (!unit || (strcmp(unit, "byte") != 0 && strcmp(unit, "nibble") != 0))) {
    log_fatal("Checksum \"unit\" must be \"byte\" or \"nibble\"");
    return 1;
  }
  int width = unit && strcmp(unit, "nibble") == 0 ? 4 : 8;
  const char *unitName = width == 4 ? "nibble" : "byte";

  size_t atCount = 0;
  long *at = readResultUnits(spec, &atCount);
  if (!at)
    return 1;
  int resultWidth = width * atCount;
  if (resultWidth > 32) {
    log_fatal("Checksum result of %d bits is wider than 32 bits", resultWidth);
    free(at);
    return 1;
  }
  unsigned mask = resultWidth >= 32 ? 0xffffffffu : (1u << resultWidth) - 1;

  long long init = 0, poly = 0, xorout = 0;
  int invert = 0, negate = 0, reflected = 0;
  if (readNumber(spec, "init", 0, mask, &init) ||
      readNumber(spec, "xorout", 0, mask, &xorout) ||
      readNumber(spec, "poly", kind == CRC, mask, &poly) ||
      readFlag(spec, "invert", &invert) ||
      readFlag(spec, "negate", &negate) ||
      readFlag(spec, "reflect", &reflected)) {
    free(at);
    return 1;
  }
  if (kind != CRC && (json_object_get_value(spec, "poly") || json_object_get_value(spec, "reflect"))) {
    log_fatal("Checksum \"poly\" and \"reflect\" are only for \"crc\"");
    free(at);
    return 1;
  }

  const char *bitorder = json_object_get_string(spec, "bitorder");
  if (json_object_get_value(spec, "bitorder") &&
      (!bitorder || (strcmp(bitorder, "lsb") != 0 && strcmp(bitorder, "msb") != 0))) {
    log_fatal("Checksum \"bitorder\" must be \"lsb\" or \"msb\"");
    free(at);
    return 1;
  }
  int msbFirst = bitorder && strcmp(bitorder, "msb") == 0;

  const char *bits = json_object_get_string(spec, "bits");
  if (json_object_get_value(spec, "bits") &&
      (!bits || strlen(bits) != 2 || bits[0] == bits[1])) {
    log_fatal("Checksum \"bits\" must be two different symbols, e.g. \"01\"");
    free(at);
    return 1;
  }
  char zero = bits ? bits[0] : '0';
  char one = bits ? bits[1] : '1';

  const char *restart = json_object_get_string(spec, "restart");
  if (json_object_get_value(spec, "restart") &&
      (!restart || strchr(restart, zero) || strchr(restart, one))) {
    log_fatal("Checksum \"restart\" must be a string of symbols that are not bits");
    free(at);
    return 1;
  }

  size_t count = 0;
  long *units = readCoveredUnits(spec, &count);
  if (!units) {
    free(at);
    return 1;
  }

  // Positions of the bit symbols in the code, without the bits of a partial
  // unit before a restart symbol
  size_t bitCountInCode = 0, unitFill = 0;
  for (size_t i = 0; code[i]; i++) {
    if (code[i] == zero || code[i] == one) {
      bitPos[bitCountInCode++] = i;
      unitFill = (unitFill + 1) % width;
    } else if (restart && strchr(restart, code[i])) {
      bitCountInCode -= unitFill;
      unitFill = 0;
    }
  }
  long unitCount = bitCountInCode / width;

  int result = 0;
  long last = 0;
  for (size_t k = 0; k < atCount && !result; k++) {
    if (at[k] > last)
      last = at[k];
    for (size_t j = 0; j < k; j++)
      if (at[j] == at[k]) {
	log_fatal("Checksum writes %s %ld twice", unitName, at[k]);
	result = 1;
      }
    for (size_t i = 0; i < count && !result; i++)
      if (units[i] == at[k]) {
	log_fatal("Checksum %s %ld is one of the %ss it covers", unitName, at[k], unitName);
	result = 1;
      }
  }
  for (size_t i = 0; i < count; i++)
    if (units[i] > last)
      last = units[i];
  if (!result && last >= unitCount) {
    log_fatal("Checksum uses %s %ld, but the code has %ld %ss", unitName, last, unitCount, unitName);
    result = 1;
  }
  if (result) {
    free(units);
    free(at);
    return 1;
  }

  unsigned value = kind == CRC && reflected ? reflect(init, resultWidth) : (unsigned)init;
  for (size_t i = 0; i < count; i++) {
    unsigned u = readUnit(code, bitPos, units[i], width, msbFirst, one);
    switch (kind) {
    case SUM: value += u; break;
    case XOR: value ^= u; break;
    case POPCOUNT: value += bitCount(u); break;
    default: value = crcUnit(value, u, width, poly, resultWidth, reflected, mask); break;
    }
  }
  free(units);
  if (invert)
    value = ~value;
  if (negate)
    value = 0u - value;
  value = (value ^ (unsigned)xorout) & mask;
  // The units of "at" hold the result in the bit order: with "lsb" the first
  // unit holds the lowest bits, with "msb" the highest
  for (size_t k = 0; k < atCount; k++) {
    int shift = width * (msbFirst ? atCount - 1 - k : k);
    writeUnit(code, bitPos, at[k], width, msbFirst, zero, one, (value >> shift) & ((1u << width) - 1));
  }
  log_trace("Checksum %s of %zu %ss into %s %ld: 0x%x", type, count, unitName, unitName, at[0], value);
  free(at);
  return 0;
}

int applyChecksums(const JSON_Array *checksums, char *code)
{
  size_t count = json_array_get_count(checksums);
  if (!count)
    return 0;
  size_t *bitPos = malloc((strlen(code) + 1) * sizeof(size_t));
  if (!bitPos)
    return 1;
  int result = 0;
  for (size_t i = 0; i < count && !result; i++) {
    const JSON_Object *spec = json_array_get_object(checksums, i);
    if (!spec) {
      log_fatal("Checksum %zu is not an object", i);
      result = 1;
    } else {
      result = applyChecksum(spec, code, bitPos);
    }
  }
  free(bitPos);
  return result;
}

int applyConfigChecksums(char *code)
{
  return applyChecksums(loadChecksums(), code);
}
