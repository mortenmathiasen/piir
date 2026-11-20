#include <stdlib.h>
#include <string.h>
#include "checksum.h"
#include "config.h"
#include "irgeneric.h"
#include "log.h"

// Read an optional whole number between 0 and max. Returns 0 when it is
// valid or missing (then *value is left as it is).
static int readNumber(const JSON_Object *spec, const char *name, int required,
		      long max, long *value)
{
  JSON_Value *v = json_object_get_value(spec, name);
  if (!v) {
    if (required)
      log_fatal("Checksum needs \"%s\"", name);
    return required;
  }
  double d = json_value_get_number(v);
  if (json_value_get_type(v) != JSONNumber || d < 0 || d > max || d != (double)(long)d) {
    log_fatal("Checksum \"%s\" must be a whole number from 0 to %ld", name, max);
    return 1;
  }
  *value = (long)d;
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
    JSON_Array *array = json_value_get_array(data);
    *count = json_array_get_count(array);
    if (!array || !*count) {
      log_fatal("Checksum \"data\" must be a list of unit numbers");
      return NULL;
    }
    units = malloc(*count * sizeof(long));
    for (size_t i = 0; units && i < *count; i++) {
      double d = json_array_get_number(array, i);
      if (json_value_get_type(json_array_get_value(array, i)) != JSONNumber ||
	  d < 0 || d > MAX_COMMAND_SIZE || d != (double)(long)d) {
	log_fatal("Checksum \"data\" must hold whole numbers from 0 to %d", MAX_COMMAND_SIZE);
	free(units);
	return NULL;
      }
      units[i] = (long)d;
    }
    return units;
  }

  long from = 0, to = 0;
  if (readNumber(spec, "from", 1, MAX_COMMAND_SIZE, &from) ||
      readNumber(spec, "to", 1, MAX_COMMAND_SIZE, &to))
    return NULL;
  if (from > to) {
    log_fatal("Checksum \"from\" (%ld) is after \"to\" (%ld)", from, to);
    return NULL;
  }
  *count = to - from + 1;
  units = malloc(*count * sizeof(long));
  for (size_t i = 0; units && i < *count; i++)
    units[i] = from + i;
  return units;
}

static int applyChecksum(const JSON_Object *spec, char *code, size_t *bitPos)
{
  const char *type = json_object_get_string(spec, "type");
  if (!type || (strcmp(type, "sum") != 0 && strcmp(type, "xor") != 0)) {
    log_fatal("Checksum \"type\" must be \"sum\" or \"xor\"");
    return 1;
  }
  int isSum = strcmp(type, "sum") == 0;

  const char *unit = json_object_get_string(spec, "unit");
  if (json_object_get_value(spec, "unit") &&
      (!unit || (strcmp(unit, "byte") != 0 && strcmp(unit, "nibble") != 0))) {
    log_fatal("Checksum \"unit\" must be \"byte\" or \"nibble\"");
    return 1;
  }
  int width = unit && strcmp(unit, "nibble") == 0 ? 4 : 8;
  const char *unitName = width == 4 ? "nibble" : "byte";
  unsigned mask = (1u << width) - 1;

  long at = 0, init = 0;
  int invert = 0, negate = 0;
  if (readNumber(spec, "at", 1, MAX_COMMAND_SIZE, &at) ||
      readNumber(spec, "init", 0, mask, &init) ||
      readFlag(spec, "invert", &invert) ||
      readFlag(spec, "negate", &negate))
    return 1;

  const char *bitorder = json_object_get_string(spec, "bitorder");
  if (json_object_get_value(spec, "bitorder") &&
      (!bitorder || (strcmp(bitorder, "lsb") != 0 && strcmp(bitorder, "msb") != 0))) {
    log_fatal("Checksum \"bitorder\" must be \"lsb\" or \"msb\"");
    return 1;
  }
  int msbFirst = bitorder && strcmp(bitorder, "msb") == 0;

  const char *bits = json_object_get_string(spec, "bits");
  if (json_object_get_value(spec, "bits") &&
      (!bits || strlen(bits) != 2 || bits[0] == bits[1])) {
    log_fatal("Checksum \"bits\" must be two different symbols, e.g. \"01\"");
    return 1;
  }
  char zero = bits ? bits[0] : '0';
  char one = bits ? bits[1] : '1';

  const char *restart = json_object_get_string(spec, "restart");
  if (json_object_get_value(spec, "restart") &&
      (!restart || strchr(restart, zero) || strchr(restart, one))) {
    log_fatal("Checksum \"restart\" must be a string of symbols that are not bits");
    return 1;
  }

  size_t count = 0;
  long *units = readCoveredUnits(spec, &count);
  if (!units)
    return 1;

  // Positions of the bit symbols in the code, without the bits of a partial
  // unit before a restart symbol
  size_t bitCount = 0, unitFill = 0;
  for (size_t i = 0; code[i]; i++) {
    if (code[i] == zero || code[i] == one) {
      bitPos[bitCount++] = i;
      unitFill = (unitFill + 1) % width;
    } else if (restart && strchr(restart, code[i])) {
      bitCount -= unitFill;
      unitFill = 0;
    }
  }
  long unitCount = bitCount / width;

  long last = at;
  for (size_t i = 0; i < count; i++) {
    if (units[i] == at) {
      log_fatal("Checksum %s %ld is one of the %ss it covers", unitName, at, unitName);
      free(units);
      return 1;
    }
    if (units[i] > last)
      last = units[i];
  }
  if (last >= unitCount) {
    log_fatal("Checksum uses %s %ld, but the code has %ld %ss", unitName, last, unitCount, unitName);
    free(units);
    return 1;
  }

  unsigned value = init;
  for (size_t i = 0; i < count; i++) {
    unsigned u = readUnit(code, bitPos, units[i], width, msbFirst, one);
    value = isSum ? value + u : value ^ u;
  }
  free(units);
  if (invert)
    value = ~value;
  if (negate)
    value = 0u - value;
  writeUnit(code, bitPos, at, width, msbFirst, zero, one, value & mask);
  log_trace("Checksum %s of %zu %ss into %s %ld: 0x%x", type, count, unitName, unitName, at, value & mask);
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
