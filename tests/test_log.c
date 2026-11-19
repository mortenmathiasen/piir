#include "unittest.h"
#include "log.h"

static void test_level_names(void) {
  CHECK_EQ_STR("TRACE", log_level_string(LOG_TRACE));
  CHECK_EQ_STR("FATAL", log_level_string(LOG_FATAL));
  CHECK_EQ_STR("NOTHING", log_level_string(LOG_NOTHING));
  for (int level = LOG_TRACE; level <= LOG_NOTHING; level++)
    CHECK_EQ_INT(level, log_level_int(log_level_string(level)));
}

static void test_unknown_level_logs_nothing(void) {
  CHECK(log_level_int("LOUD") > LOG_NOTHING);
  CHECK(log_level_int("debug") > LOG_NOTHING); // names are case sensitive
}

static void test_set_level(void) {
  log_set_level(LOG_WARN);
  CHECK_EQ_INT(LOG_WARN, log_get_level());
}

static int captured = 0;
static int capturedLevel = -1;
static void capture(log_Event *ev) {
  captured++;
  capturedLevel = ev->level;
}

static void test_callback_level_filter(void) {
  log_set_quiet(true);
  CHECK_EQ_INT(0, log_add_callback(capture, NULL, LOG_WARN));
  log_info("ignored");
  CHECK_EQ_INT(0, captured);
  log_error("reported %d", 42);
  CHECK_EQ_INT(1, captured);
  CHECK_EQ_INT(LOG_ERROR, capturedLevel);
  log_set_quiet(false);
}

int main(void) {
  RUN_TEST(test_level_names);
  RUN_TEST(test_unknown_level_logs_nothing);
  RUN_TEST(test_set_level);
  RUN_TEST(test_callback_level_filter);
  return TEST_RESULT();
}
