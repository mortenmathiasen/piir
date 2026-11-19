#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "unittest.h"
#include "state.h"
#include "log.h"

static char dir[] = "/tmp/piir-state-XXXXXX";
static char file[128];

static void test_state_file_location(void) {
  setenv("PIIR_STATE_DIR", "/some/dir", 1);
  char *path = toggleStateFile();
  CHECK_EQ_STR("/some/dir/toggle-state", path);
  free(path);
  unsetenv("PIIR_STATE_DIR");

  if (geteuid() == 0) {
    path = toggleStateFile();
    CHECK_EQ_STR("/var/lib/piir/toggle-state", path);
    free(path);
    return;
  }
  setenv("XDG_STATE_HOME", "/xdg", 1);
  path = toggleStateFile();
  CHECK_EQ_STR("/xdg/piir/toggle-state", path);
  free(path);
  unsetenv("XDG_STATE_HOME");

  char *home = getenv("HOME") ? strdup(getenv("HOME")) : NULL;
  setenv("HOME", "/home/pi", 1);
  path = toggleStateFile();
  CHECK_EQ_STR("/home/pi/.local/state/piir/toggle-state", path);
  free(path);
  unsetenv("HOME");
  CHECK(toggleStateFile() == NULL);
  if (home) {
    setenv("HOME", home, 1);
    free(home);
  }
}

static void test_read_write(void) {
  CHECK_EQ_INT(0, readToggleState(file, "/remotes/a.json"));   // no file yet
  CHECK_EQ_INT(0, writeToggleState(file, "/remotes/a.json", 1));
  CHECK_EQ_INT(1, readToggleState(file, "/remotes/a.json"));
  CHECK_EQ_INT(0, readToggleState(file, "/remotes/b.json"));
  CHECK_EQ_INT(0, writeToggleState(file, "/remotes/b.json", 1));
  CHECK_EQ_INT(0, writeToggleState(file, "/remotes/a.json", 0));
  CHECK_EQ_INT(0, readToggleState(file, "/remotes/a.json"));
  CHECK_EQ_INT(1, readToggleState(file, "/remotes/b.json"));
  CHECK_EQ_INT(0, writeToggleState(file, "/remotes/a.json", 5));   // any true value is 1
  CHECK_EQ_INT(1, readToggleState(file, "/remotes/a.json"));

  // One line per remote
  FILE *f = fopen(file, "r");
  char text[256] = {0};
  size_t n = fread(text, 1, sizeof(text) - 1, f);
  fclose(f);
  CHECK(n > 0);
  CHECK_EQ_STR("1 /remotes/b.json\n1 /remotes/a.json\n", text);
}

static void test_ignores_garbage(void) {
  FILE *f = fopen(file, "w");
  fputs("garbage\n1 /remotes/c.json", f);   // no newline at the end
  fclose(f);
  CHECK_EQ_INT(1, readToggleState(file, "/remotes/c.json"));
  CHECK_EQ_INT(0, writeToggleState(file, "/remotes/d.json", 1));
  CHECK_EQ_INT(1, readToggleState(file, "/remotes/c.json"));
  CHECK_EQ_INT(1, readToggleState(file, "/remotes/d.json"));
}

static void test_creates_directories(void) {
  char nested[160];
  snprintf(nested, sizeof(nested), "%s/a/b/toggle-state", dir);
  CHECK_EQ_INT(0, writeToggleState(nested, "/remotes/a.json", 1));
  CHECK_EQ_INT(1, readToggleState(nested, "/remotes/a.json"));
  unlink(nested);
  snprintf(nested, sizeof(nested), "%s/a/b", dir);
  rmdir(nested);
  snprintf(nested, sizeof(nested), "%s/a", dir);
  rmdir(nested);
}

static void test_errors(void) {
  CHECK_EQ_INT(0, readToggleState(NULL, "/remotes/a.json"));
  CHECK_EQ_INT(-1, writeToggleState(NULL, "/remotes/a.json", 1));
  // A file where a directory is needed
  char bad[160];
  snprintf(bad, sizeof(bad), "%s/toggle-state/below", dir);
  CHECK_EQ_INT(-1, writeToggleState(bad, "/remotes/a.json", 1));
  // A directory where the file is needed
  CHECK_EQ_INT(-1, writeToggleState(dir, "/remotes/a.json", 1));
  CHECK_EQ_INT(0, readToggleState(dir, "/remotes/a.json"));
}

int main(void) {
  log_set_level(LOG_NOTHING);
  if (!mkdtemp(dir))
    return 2;
  snprintf(file, sizeof(file), "%s/toggle-state", dir);
  RUN_TEST(test_state_file_location);
  RUN_TEST(test_read_write);
  RUN_TEST(test_ignores_garbage);
  RUN_TEST(test_creates_directories);
  RUN_TEST(test_errors);
  unlink(file);
  rmdir(dir);
  return TEST_RESULT();
}
