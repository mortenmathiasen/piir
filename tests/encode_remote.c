// Print the codes irlingo sends for feature sets, without transmitting.
//
// usage: encode_remote REMOTE.json < FEATURES
// Each line of FEATURES holds NAME=VALUE pairs separated by spaces; the code
// for them (features, checksums and plugin applied) is printed on a line.
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "checksum.h"
#include "plugins.h"
#include "log.h"

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s REMOTE.json < FEATURES\n", argv[0]);
    return 2;
  }
  log_set_level(LOG_ERROR);
  loadConfig(argv[1]);
  if (!isConfigLoaded())
    return 2;
  const char *plugin = NULL;
  loadPlugin(&plugin);
  if (!plugin && !loadChecksums() && !hasVariants())
    plugin = legacyRemotePlugin(argv[1]);

  size_t featureCount = getAllFeaturesCount();
  featureT *features = calloc(featureCount ? featureCount : 1, sizeof(featureT));
  char line[4096];
  int result = 0;
  while (fgets(line, sizeof(line), stdin)) {
    memset(features, 0, featureCount * sizeof(featureT));
    for (char *pair = strtok(line, " \t\r\n"); pair; pair = strtok(NULL, " \t\r\n")) {
      char *value = strchr(pair, '=');
      size_t index = 0;
      if (!value) {
	fprintf(stderr, "Not NAME=VALUE: %s\n", pair);
	return 2;
      }
      *value++ = 0;
      if (!hasFeatureNameAndValue(pair, value) || !getFeatureIndex(pair, &index)) {
	fprintf(stderr, "Unknown feature %s=%s\n", pair, value);
	return 2;
      }
      features[index].name = pair;
      features[index].value = value;
    }
    char *code = NULL;
    loadFeaturedCode(features, &code);
    if (applyConfigChecksums(code) || applyRemotePlugin(plugin, code))
      result = 1;
    printf("%s\n", code);
    free(code);
  }
  free(features);
  unloadConfig();
  return result;
}
