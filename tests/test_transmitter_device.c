// The libgpiod backend driving a GPIO line. libgpiod and opendir("/dev") are
// replaced at link time (-Wl,--wrap) with a simulated GPIO chip.
#include <dirent.h>
#include <gpiod.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "unittest.h"
#include "transmitter.h"
#include "log.h"

// --- simulated libgpiod
static char devDir[] = "/tmp/piir-gpiod-XXXXXX";
static const char *chipWithLine;     // "/dev/gpiochipN" holding the line, or NULL
static const char *failFunction;     // name of the libgpiod function that fails
static int lineOffset;
static int values[64];
static int valueCount;
static int chipsOpen;
static char dummy;

#define FAKE ((void *)&dummy)
#define FAILS(name) (failFunction && strcmp(failFunction, name) == 0)

DIR *__real_opendir(const char *name);
DIR *__wrap_opendir(const char *name) {
  return __real_opendir(strcmp(name, "/dev") == 0 ? devDir : name);
}

bool __wrap_gpiod_is_gpiochip_device(const char *path) {
  return strncmp(path, "/dev/gpiochip", 13) == 0;
}

struct gpiod_chip *__wrap_gpiod_chip_open(const char *path) {
  (void)path;
  if (FAILS("gpiod_chip_open"))
    return NULL;
  chipsOpen++;
  return (struct gpiod_chip *)strdup(path);
}

void __wrap_gpiod_chip_close(struct gpiod_chip *chip) {
  chipsOpen--;
  free(chip);
}

int __wrap_gpiod_chip_get_line_offset_from_name(struct gpiod_chip *chip, const char *name) {
  if (chipWithLine && strcmp((const char *)chip, chipWithLine) == 0 && strcmp(name, "GPIO17") == 0)
    return lineOffset;
  return -1;
}

struct gpiod_line_settings *__wrap_gpiod_line_settings_new(void) {
  return FAILS("gpiod_line_settings_new") ? NULL : FAKE;
}
void __wrap_gpiod_line_settings_free(struct gpiod_line_settings *s) { (void)s; }
int __wrap_gpiod_line_settings_set_direction(struct gpiod_line_settings *s, enum gpiod_line_direction d) {
  (void)s; (void)d; return 0;
}
int __wrap_gpiod_line_settings_set_output_value(struct gpiod_line_settings *s, enum gpiod_line_value v) {
  (void)s; (void)v; return 0;
}

struct gpiod_line_config *__wrap_gpiod_line_config_new(void) {
  return FAILS("gpiod_line_config_new") ? NULL : FAKE;
}
void __wrap_gpiod_line_config_free(struct gpiod_line_config *c) { (void)c; }
int __wrap_gpiod_line_config_add_line_settings(struct gpiod_line_config *c, const unsigned int *offsets,
                                               size_t n, struct gpiod_line_settings *s) {
  (void)c; (void)s;
  lineOffset = n == 1 ? (int)offsets[0] : -1;
  return FAILS("gpiod_line_config_add_line_settings") ? -1 : 0;
}

struct gpiod_request_config *__wrap_gpiod_request_config_new(void) {
  return FAILS("gpiod_request_config_new") ? NULL : FAKE;
}
void __wrap_gpiod_request_config_free(struct gpiod_request_config *c) { (void)c; }
void __wrap_gpiod_request_config_set_consumer(struct gpiod_request_config *c, const char *name) {
  (void)c; (void)name;
}

struct gpiod_line_request *__wrap_gpiod_chip_request_lines(struct gpiod_chip *chip,
    struct gpiod_request_config *rc, struct gpiod_line_config *lc) {
  (void)chip; (void)rc; (void)lc;
  return FAILS("gpiod_chip_request_lines") ? NULL : FAKE;
}
void __wrap_gpiod_line_request_release(struct gpiod_line_request *r) { (void)r; }

int __wrap_gpiod_line_request_set_value(struct gpiod_line_request *r, unsigned int offset,
                                        enum gpiod_line_value value) {
  (void)r; (void)offset;
  if (FAILS("gpiod_line_request_set_value") && valueCount >= 2)
    return -1;
  if (valueCount < 64)
    values[valueCount] = value == GPIOD_LINE_VALUE_ACTIVE;
  valueCount++;
  return 0;
}

// --- tests
static symbolDefinition SYMBOLS[] = {{'0', 100, 200}};

static void reset(const char *chip, int offset) {
  chipWithLine = chip;
  lineOffset = offset;
  failFunction = NULL;
  valueCount = 0;
  memset(values, 0, sizeof(values));
}

static void test_transmit_on_named_line(void) {
  reset("/dev/gpiochip4", 17);
  CHECK_EQ_INT(0, irSlingGeneric(17, 10000, 0.5, 1, SYMBOLS, "0"));
  CHECK_EQ_INT(17, lineOffset);
  // 100 us mark at 10 kHz is one carrier cycle: on, off, then the space,
  // which merges with the off half, and finally the LED is switched off
  CHECK_EQ_INT(3, valueCount);
  CHECK_EQ_INT(1, values[0]);
  CHECK_EQ_INT(0, values[1]);
  CHECK_EQ_INT(0, values[2]);
  CHECK_EQ_INT(0, chipsOpen);
}

static void test_fallback_to_first_chip(void) {
  reset(NULL, 0);
  CHECK_EQ_INT(0, irSlingGeneric(17, 10000, 0.5, 1, SYMBOLS, "00"));
  CHECK_EQ_INT(17, lineOffset);   // the pin number used as line offset
  CHECK_EQ_INT(5, valueCount);
  CHECK_EQ_INT(0, chipsOpen);
}

static void test_failures(void) {
  const char *functions[] = {"gpiod_chip_open", "gpiod_line_settings_new", "gpiod_line_config_new",
                             "gpiod_line_config_add_line_settings", "gpiod_request_config_new",
                             "gpiod_chip_request_lines", "gpiod_line_request_set_value"};
  for (size_t i = 0; i < sizeof(functions) / sizeof(functions[0]); i++) {
    reset("/dev/gpiochip0", 3);
    failFunction = functions[i];
    int before = unittest_failures;
    CHECK_EQ_INT(EXIT_FAILURE, irSlingGeneric(17, 10000, 0.5, 1, SYMBOLS, "0"));
    CHECK_EQ_INT(0, chipsOpen);
    if (unittest_failures != before)
      printf("  when %s fails\n", functions[i]);
  }
}

static void test_final_switch_off_fails(void) {
  reset("/dev/gpiochip0", 3);
  failFunction = "gpiod_line_request_set_value";
  // the two pulses are set, switching off afterwards fails
  CHECK_EQ_INT(EXIT_FAILURE, irSlingGeneric(17, 10000, 0.5, 1, SYMBOLS, "0"));
}

static void touch(const char *name) {
  char path[64];
  snprintf(path, sizeof(path), "%s/%s", devDir, name);
  FILE *f = fopen(path, "w");
  if (f)
    fclose(f);
}

static void removeFile(const char *name) {
  char path[64];
  snprintf(path, sizeof(path), "%s/%s", devDir, name);
  unlink(path);
}

int main(void) {
  log_set_level(LOG_NOTHING);
  if (!mkdtemp(devDir))
    return 2;
  touch("gpiochip0");
  touch("gpiochip4");
  touch("null");

  RUN_TEST(test_transmit_on_named_line);
  RUN_TEST(test_fallback_to_first_chip);
  RUN_TEST(test_failures);
  RUN_TEST(test_final_switch_off_fails);

  removeFile("gpiochip0");
  removeFile("gpiochip4");
  removeFile("null");
  rmdir(devDir);
  return TEST_RESULT();
}
