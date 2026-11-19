// Checks every remote configuration against the "tests" section of its JSON
// file: each test lists features and the symbol string a real remote sends.
//
// usage: test_remotes [--known-failures FILE] REMOTE.json...
// FILE lists "remote: test description" lines for vectors that are known not
// to match yet. They are reported but do not fail the test, and a listed
// vector that passes fails it so the list is kept up to date.
#include <stdlib.h>
#include "unittest.h"
#include "config.h"
#include "plugins.h"
#include "parson.h"
#include "log.h"

static char *knownFailures = NULL;

static void loadKnownFailures(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) {
    fprintf(stderr, "Cannot read %s\n", path);
    exit(2);
  }
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  knownFailures = calloc(size + 2, 1);
  knownFailures[0] = '\n';
  if (fread(knownFailures + 1, 1, size, f) != (size_t)size)
    exit(2);
  fclose(f);
}

static int isKnownFailure(const char *remote, const char *description) {
  if (!knownFailures)
    return 0;
  char line[512];
  snprintf(line, sizeof(line), "\n%s: %s\n", remote, description);
  return strstr(knownFailures, line) != NULL;
}

static const char *remoteName(const char *path, char *buffer, size_t size) {
  const char *slash = strrchr(path, '/');
  snprintf(buffer, size, "%s", slash ? slash + 1 : path);
  char *dot = strrchr(buffer, '.');
  if (dot)
    *dot = 0;
  return buffer;
}

static void check_remote(const char *path) {
  char nameBuffer[256];
  const char *remote = remoteName(path, nameBuffer, sizeof(nameBuffer));
  size_t passed = 0, knownFailed = 0;

  JSON_Value *root = json_parse_file(path);
  CHECK(root != NULL);
  if (!root)
    return;
  JSON_Array *tests = json_object_get_array(json_value_get_object(root), "tests");

  unloadConfig();
  loadConfig(path);
  CHECK(isConfigLoaded());
  size_t featureCount = getAllFeaturesCount();

  size_t testCount = json_array_get_count(tests);
  for (size_t t = 0; t < testCount; t++) {
    JSON_Object *test = json_array_get_object(tests, t);
    const char *description = json_object_get_string(test, "description");
    const char *outcome = json_object_get_string(test, "outcome");
    JSON_Object *wanted = json_object_get_object(test, "features");
    size_t wantedCount = json_object_get_count(wanted);

    featureT *features = calloc(featureCount ? featureCount : 1, sizeof(featureT));
    int valid = wantedCount <= featureCount;
    for (size_t f = 0; f < wantedCount && f < featureCount; f++) {
      features[f].name = json_object_get_name(wanted, f);
      features[f].value = json_object_get_string(wanted, features[f].name);
      if (!features[f].value || !hasFeatureNameAndValue(features[f].name, features[f].value))
        valid = 0;
    }

    char *code = 0;
    if (valid) {
      loadFeaturedCode(features, &code);
      applyRemotePlugins(path, code);
    }
    int ok = valid && outcome && strcmp(outcome, code) == 0;

    unittest_checks++;
    if (isKnownFailure(remote, description)) {
      if (ok) {
        unittest_failures++;
        printf("FIXED %s: %s (remove it from the known failures)\n", remote, description);
      } else {
        knownFailed++;
        printf("KNOWN FAILURE %s: %s\n", remote, description);
      }
    } else if (ok) {
      passed++;
    } else {
      unittest_failures++;
      printf("FAIL %s: %s\n", remote, description);
      if (!valid)
        printf("  features do not match the configuration\n");
      else
        printf("  expected %s\n  got      %s\n", outcome ? outcome : "(no outcome)", code);
    }

    free(code);
    free(features);
  }
  printf("%s: %zu test vectors, %zu passed, %zu known failures\n",
         remote, testCount, passed, knownFailed);
  json_value_free(root);
}

int main(int argc, char *argv[]) {
  log_set_level(LOG_NOTHING);
  int first = 1;
  if (argc > 2 && strcmp(argv[1], "--known-failures") == 0) {
    loadKnownFailures(argv[2]);
    first = 3;
  }
  if (argc <= first) {
    fprintf(stderr, "usage: %s [--known-failures FILE] REMOTE.json...\n", argv[0]);
    return 2;
  }
  for (int i = first; i < argc; i++)
    check_remote(argv[i]);
  unloadConfig();
  free(knownFailures);
  return TEST_RESULT();
}
