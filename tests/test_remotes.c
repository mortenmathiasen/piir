// Checks remote configurations:
// - every value of every feature produces a code that only uses defined
//   symbols and fits irlingo's limits
// - the "tests" section of a remote, where present, lists features and the
//   code a real remote sends for them, and each must match
//
// usage: test_remotes [--known-failures FILE] PATH...
// PATH is a remote JSON file or a directory searched recursively.
// FILE lists "remote: test description" lines for test vectors known not to
// match yet. They are reported but do not fail the test, and a listed vector
// that passes fails it so the list is kept up to date.
#include <dirent.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "unittest.h"
#include "config.h"
#include "irgeneric.h"
#include "plugins.h"
#include "checksum.h"
#include "parson.h"
#include "log.h"

// Values the kernel accepts in one write (src/lirc.h)
#define MAX_VALUES 1024

static char *knownFailures = NULL;
static size_t remotesChecked = 0, keysChecked = 0, vectorsChecked = 0, knownFailed = 0;

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

// Count the pulses and spaces a code sends, merging like the backends do.
// Returns -1 if the code uses an undefined symbol.
static long countValues(const char *code, const symbolDefinition *symbols, size_t symbolCount) {
  long count = 0;
  int lastIsPulse = 0;
  for (const char *c = code; *c; c++) {
    const symbolDefinition *s = NULL;
    for (size_t i = 0; i < symbolCount; i++)
      if (symbols[i].symbol == *c)
        s = &symbols[i];
    if (!s)
      return -1;
    if (s->markDuration && !(count && lastIsPulse)) {
      count++;
      lastIsPulse = 1;
    }
    if (s->spaceDuration && count && lastIsPulse) {
      count++;
      lastIsPulse = 0;
    }
  }
  return count;
}

static int checkCode(const char *path, const char *what, const char *code,
                     const symbolDefinition *symbols, size_t symbolCount) {
  long values = countValues(code, symbols, symbolCount);
  int ok = 1;
  if (values < 0) {
    printf("FAIL %s: %s uses an undefined symbol\n", path, what);
    ok = 0;
  } else if (values == 0) {
    printf("FAIL %s: %s sends nothing\n", path, what);
    ok = 0;
  } else if (values > MAX_VALUES) {
    printf("FAIL %s: %s needs %ld pulses and spaces, more than %d\n", path, what, values, MAX_VALUES);
    ok = 0;
  } else if (strlen(code) > MAX_COMMAND_SIZE) {
    printf("FAIL %s: %s is longer than %d symbols\n", path, what, MAX_COMMAND_SIZE);
    ok = 0;
  }
  unittest_checks++;
  if (!ok)
    unittest_failures++;
  return ok;
}

static void checkKeys(const char *path, const symbolDefinition *symbols, size_t symbolCount) {
  size_t featureCount = getAllFeaturesCount();
  const char *plugin = NULL;
  loadPlugin(&plugin);
  featureT *features = calloc(featureCount ? featureCount : 1, sizeof(featureT));
  for (size_t f = 0; f < featureCount; f++) {
    size_t valueCount = getFeatureValuesCount(f);
    for (size_t v = 0; v < valueCount; v++) {
      memset(features, 0, featureCount * sizeof(featureT));
      features[f].name = getFeatureName(f);
      features[f].value = getFeatureValueName(f, v);
      for (int toggled = 0; toggled <= hasToggle(); toggled++) {
        char *code = NULL;
        loadToggledFeaturedCode(features, toggled, &code);
        int checksumOk = applyConfigChecksums(code) == 0;
        unittest_checks++;
        if (!checksumOk) {
          unittest_failures++;
          printf("FAIL %s: invalid checksum\n", path);
        }
        int pluginOk = applyRemotePlugin(plugin, code) == 0;
        unittest_checks++;
        if (!pluginOk) {
          unittest_failures++;
          printf("FAIL %s: unknown plugin %s\n", path, plugin);
        }
        char what[256];
        snprintf(what, sizeof(what), "%s=%s%s", features[f].name, features[f].value,
                 toggled ? " (toggled)" : "");
        checkCode(path, what, code, symbols, symbolCount);
        free(code);
      }
      keysChecked++;
    }
  }
  free(features);
}

static void checkVectors(const char *path, JSON_Array *tests) {
  char nameBuffer[256];
  const char *remote = remoteName(path, nameBuffer, sizeof(nameBuffer));
  const char *plugin = NULL;
  loadPlugin(&plugin);
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
      valid = applyConfigChecksums(code) == 0;
      applyRemotePlugin(plugin, code);
    }
    int ok = valid && outcome && strcmp(outcome, code) == 0;

    unittest_checks++;
    vectorsChecked++;
    if (isKnownFailure(remote, description)) {
      if (ok) {
        unittest_failures++;
        printf("FIXED %s: %s (remove it from the known failures)\n", remote, description);
      } else {
        knownFailed++;
        printf("KNOWN FAILURE %s: %s\n", remote, description);
      }
    } else if (!ok) {
      unittest_failures++;
      printf("FAIL %s: %s\n", remote, description);
      if (!valid)
        printf("  features do not match the configuration, or a checksum is invalid\n");
      else
        printf("  expected %s\n  got      %s\n", outcome ? outcome : "(no outcome)", code);
    }

    free(code);
    free(features);
  }
}

static void checkRemote(const char *path) {
  unloadConfig();
  loadConfig(path);
  unittest_checks++;
  if (!isConfigLoaded()) {
    unittest_failures++;
    printf("FAIL %s: cannot be loaded\n", path);
    return;
  }
  remotesChecked++;

  size_t symbolCount = getSymboldefinitionsCount();
  symbolDefinition *symbols = calloc(symbolCount ? symbolCount : 1, sizeof(symbolDefinition));
  loadConfigSymbols(symbols);
  const char *templateCode = NULL;
  loadTemplateCode(&templateCode);
  unittest_checks++;
  if (!templateCode || getAllFeaturesCount() == 0) {
    unittest_failures++;
    printf("FAIL %s: no template or no features\n", path);
  } else {
    checkKeys(path, symbols, symbolCount);
  }
  free(symbols);

  JSON_Value *root = json_parse_file(path);
  JSON_Array *tests = json_object_get_array(json_value_get_object(root), "tests");
  if (tests)
    checkVectors(path, tests);
  json_value_free(root);
}

static int hasJsonSuffix(const char *name) {
  size_t len = strlen(name);
  return len > 5 && strcmp(name + len - 5, ".json") == 0;
}

static int compareStrings(const void *a, const void *b) {
  return strcmp(*(char *const *)a, *(char *const *)b);
}

static void checkPath(const char *path) {
  struct stat st;
  if (stat(path, &st) != 0) {
    unittest_checks++;
    unittest_failures++;
    printf("FAIL %s: not found\n", path);
    return;
  }
  if (!S_ISDIR(st.st_mode)) {
    checkRemote(path);
    return;
  }
  DIR *dir = opendir(path);
  if (!dir)
    return;
  char **entries = NULL;
  size_t count = 0;
  struct dirent *entry;
  while ((entry = readdir(dir))) {
    if (entry->d_name[0] == '.')
      continue;
    entries = realloc(entries, (count + 1) * sizeof(char *));
    entries[count] = malloc(strlen(path) + strlen(entry->d_name) + 2);
    sprintf(entries[count], "%s/%s", path, entry->d_name);
    count++;
  }
  closedir(dir);
  qsort(entries, count, sizeof(char *), compareStrings);
  for (size_t i = 0; i < count; i++) {
    struct stat est;
    if (stat(entries[i], &est) == 0 && (S_ISDIR(est.st_mode) || hasJsonSuffix(entries[i])))
      checkPath(entries[i]);
    free(entries[i]);
  }
  free(entries);
}

int main(int argc, char *argv[]) {
  log_set_level(LOG_NOTHING);
  int first = 1;
  if (argc > 2 && strcmp(argv[1], "--known-failures") == 0) {
    loadKnownFailures(argv[2]);
    first = 3;
  }
  if (argc <= first) {
    fprintf(stderr, "usage: %s [--known-failures FILE] PATH...\n", argv[0]);
    return 2;
  }
  for (int i = first; i < argc; i++)
    checkPath(argv[i]);
  unloadConfig();
  free(knownFailures);
  printf("%zu remotes, %zu keys, %zu test vectors (%zu known failures)\n",
         remotesChecked, keysChecked, vectorsChecked, knownFailed);
  return TEST_RESULT();
}
