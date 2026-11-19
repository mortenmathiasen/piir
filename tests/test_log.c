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

static int lockCalls = 0;
static void countingLock(bool lock, void *udata) {
  (void)lock;
  (void)udata;
  lockCalls++;
}

static void test_file_logging_and_lock(void) {
  FILE *f = tmpfile();
  CHECK(f != NULL);
  log_set_quiet(true);
  log_set_lock(countingLock, NULL);
  CHECK_EQ_INT(0, log_add_fp(f, LOG_WARN));
  log_debug("not in the file");
  log_warn("in the file %d", 7);
  log_set_lock(NULL, NULL);
  log_set_quiet(false);
  CHECK_EQ_INT(4, lockCalls);   // lock and unlock for each message

  char text[256] = {0};
  rewind(f);
  size_t n = fread(text, 1, sizeof(text) - 1, f);
  fclose(f);
  CHECK(n > 0);
  CHECK(strstr(text, "WARN") != NULL);
  CHECK(strstr(text, "in the file 7") != NULL);
  CHECK(strstr(text, "not in the file") == NULL);
}

static void test_too_many_callbacks(void) {
  int result = 0;
  for (int i = 0; i < 40 && result == 0; i++)
    result = log_add_callback(capture, NULL, LOG_NOTHING);
  CHECK_EQ_INT(-1, result);
}

static void test_stderr_logging(void) {
  log_set_level(LOG_TRACE);
  log_trace("written to stderr");
  log_set_level(LOG_NOTHING);
}

int main(void) {
  RUN_TEST(test_level_names);
  RUN_TEST(test_unknown_level_logs_nothing);
  RUN_TEST(test_set_level);
  RUN_TEST(test_callback_level_filter);
  RUN_TEST(test_file_logging_and_lock);
  RUN_TEST(test_stderr_logging);
  RUN_TEST(test_too_many_callbacks);
  return TEST_RESULT();
}
