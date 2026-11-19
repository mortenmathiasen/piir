#include <stdlib.h>
#include "unittest.h"
#include "config.h"
#include "log.h"

#define SIMPLE FIXTURE_DIR "/simple.json"

static void test_load_missing_file(void) {
  unloadConfig();
  loadConfig(FIXTURE_DIR "/does_not_exist.json");
  CHECK(!isConfigLoaded());
}

static void test_load_invalid_json(void) {
  unloadConfig();
  loadConfig(FIXTURE_DIR "/invalid.json");
  CHECK(!isConfigLoaded());
}

static void test_load_non_object_root(void) {
  unloadConfig();
  loadConfig(FIXTURE_DIR "/array_root.json");
  CHECK(!isConfigLoaded());
}

static void test_failed_load_keeps_previous(void) {
  unloadConfig();
  loadConfig(SIMPLE);
  loadConfig(FIXTURE_DIR "/invalid.json");
  CHECK(isConfigLoaded());
  const char *description = 0;
  loadDescription(&description);
  CHECK_EQ_STR("Test remote", description);
}

static void test_unload(void) {
  loadConfig(SIMPLE);
  CHECK(isConfigLoaded());
  unloadConfig();
  CHECK(!isConfigLoaded());
  unloadConfig(); // twice is harmless
  CHECK(!isConfigLoaded());
}

static void test_scalar_settings(void) {
  loadConfig(SIMPLE);

  const char *description = 0;
  loadDescription(&description);
  CHECK_EQ_STR("Test remote", description);

  int frequency = 0;
  loadFrequency(&frequency);
  CHECK_EQ_INT(36000, frequency);

  double dutycycle = 0;
  loadDutyCycle(&dutycycle);
  CHECK(dutycycle > 0.329 && dutycycle < 0.331);

  unsigned int outPin = 0;
  loadOutPin(&outPin);
  CHECK_EQ_INT(22, outPin);

  const char *device = 0;
  loadDevice(&device);
  CHECK_EQ_STR("/dev/lirc7", device);

  const char *templateCode = 0;
  loadTemplateCode(&templateCode);
  CHECK_EQ_STR("S-0000-0000", templateCode);
}

static void test_missing_optional_device(void) {
  unloadConfig();
  loadConfig(PROJECT_DIR "/conf/remotes/panasonic/hvac_panasonic.json");
  CHECK(isConfigLoaded());
  const char *device = (const char *)1;
  loadDevice(&device);
  CHECK(device == NULL);
}

static void test_features(void) {
  loadConfig(SIMPLE);

  CHECK_EQ_INT(3, getAllFeaturesCount());
  CHECK_EQ_STR("power", getFeatureName(0));
  CHECK_EQ_STR("level", getFeatureName(1));

  CHECK_EQ_INT(2, getFeatureValuesCount(0));
  CHECK_EQ_STR("ON", getFeatureValueName(0, 0));
  CHECK_EQ_STR("OFF", getFeatureValueName(0, 1));
  CHECK_EQ_INT(3, getFeatureValuesCount(1));
  CHECK_EQ_STR("MAX", getFeatureValueName(1, 2));

  CHECK(hasFeatureNameAndValue("power", "ON"));
  CHECK(hasFeatureNameAndValue("level", "MAX"));
  CHECK(!hasFeatureNameAndValue("power", "MAX"));
  CHECK(!hasFeatureNameAndValue("power", "on"));
  CHECK(!hasFeatureNameAndValue("volume", "ON"));

  size_t index = 99;
  CHECK(getFeatureIndex("level", &index));
  CHECK_EQ_INT(1, index);
  CHECK(getFeatureIndex("preset", &index));
  CHECK_EQ_INT(2, index);
  index = 99;
  CHECK(!getFeatureIndex("volume", &index));
  CHECK_EQ_INT(99, index);
}

static void test_symbol_definitions(void) {
  loadConfig(SIMPLE);

  size_t count = getSymboldefinitionsCount();
  CHECK_EQ_INT(4, count);

  symbolDefinition symbols[4];
  memset(symbols, 0, sizeof(symbols));
  loadConfigSymbols(symbols);
  CHECK_EQ_INT('S', symbols[0].symbol);
  CHECK_EQ_INT(9000, symbols[0].markDuration);
  CHECK_EQ_INT(4500, symbols[0].spaceDuration);
  CHECK_EQ_INT('1', symbols[2].symbol);
  CHECK_EQ_INT(560, symbols[2].markDuration);
  CHECK_EQ_INT(1690, symbols[2].spaceDuration);
  CHECK_EQ_INT('-', symbols[3].symbol);
  CHECK_EQ_INT(0, symbols[3].markDuration);
}

static void test_featured_code_without_features(void) {
  loadConfig(SIMPLE);
  char *code = 0;
  loadFeaturedCode(NULL, &code);
  CHECK_EQ_STR("S-0000-0000", code);
  free(code);

  featureT none[3] = {{0, 0}, {0, 0}, {0, 0}};
  loadFeaturedCode(none, &code);
  CHECK_EQ_STR("S-0000-0000", code);
  free(code);
}

static void test_featured_code_single_feature(void) {
  loadConfig(SIMPLE);
  featureT features[3] = {{"power", "ON"}, {0, 0}, {0, 0}};
  char *code = 0;
  loadFeaturedCode(features, &code);
  CHECK_EQ_STR("S-1111-0000", code);
  free(code);
}

static void test_featured_code_combined_features(void) {
  loadConfig(SIMPLE);
  featureT features[3] = {{"level", "HIGH"}, {"power", "ON"}, {0, 0}};
  char *code = 0;
  loadFeaturedCode(features, &code);
  CHECK_EQ_STR("S-1111-1000", code);
  free(code);
}

static void test_featured_code_later_feature_wins(void) {
  loadConfig(SIMPLE);
  featureT features[3] = {{"level", "MAX"}, {"level", "LOW"}, {0, 0}};
  char *code = 0;
  loadFeaturedCode(features, &code);
  // The value given last overwrites the digits it defines
  CHECK_EQ_STR("S-0000-0001", code);
  free(code);
}

static void test_featured_code_configuration_order(void) {
  loadConfig(SIMPLE);
  // preset comes last in the configuration file, so it overrides power and
  // level even when they are requested after it
  featureT features[3] = {{"preset", "NIGHT"}, {"level", "MAX"}, {"power", "ON"}};
  char *code = 0;
  loadFeaturedCode(features, &code);
  CHECK_EQ_STR("S-0011-0000", code);
  free(code);

  // Spaces in a later feature keep what earlier features set
  featureT keep[3] = {{"preset", "NONE"}, {"level", "MAX"}, {"power", "ON"}};
  loadFeaturedCode(keep, &code);
  CHECK_EQ_STR("S-1111-1111", code);
  free(code);
}

static void test_featured_code_unknown_value(void) {
  loadConfig(SIMPLE);
  featureT features[3] = {{"power", "SIDEWAYS"}, {"level", "LOW"}, {0, 0}};
  char *code = 0;
  loadFeaturedCode(features, &code);
  CHECK_EQ_STR("S-0000-0001", code);
  free(code);
}

static void test_minimal_remote(void) {
  unloadConfig();
  loadConfig(FIXTURE_DIR "/minimal.json");
  CHECK(isConfigLoaded());
  CHECK_EQ_INT(0, getAllFeaturesCount());
  CHECK_EQ_INT(0, getSymboldefinitionsCount());
  symbolDefinition none[1];
  loadConfigSymbols(none);   // logs that there are no symbol definitions
  char *code = 0;
  featureT features[1] = {{0, 0}};
  loadFeaturedCode(features, &code);
  CHECK_EQ_STR("", code);
  free(code);
  const char *plugin = (const char *)1;
  loadPlugin(&plugin);
  CHECK(plugin == NULL);
}

static void test_feature_longer_than_template(void) {
  unloadConfig();
  loadConfig(FIXTURE_DIR "/long_feature.json");
  featureT features[1] = {{"key", "LONG"}};
  char *code = 0;
  loadFeaturedCode(features, &code);
  CHECK_EQ_STR("BB", code);
  free(code);
  features[0].value = "SHORT";
  loadFeaturedCode(features, &code);
  CHECK_EQ_STR("BA", code);
  free(code);
}

int main(void) {
  log_set_level(LOG_NOTHING);
  RUN_TEST(test_load_missing_file);
  RUN_TEST(test_load_invalid_json);
  RUN_TEST(test_load_non_object_root);
  RUN_TEST(test_failed_load_keeps_previous);
  RUN_TEST(test_unload);
  RUN_TEST(test_scalar_settings);
  RUN_TEST(test_missing_optional_device);
  RUN_TEST(test_features);
  RUN_TEST(test_symbol_definitions);
  RUN_TEST(test_featured_code_without_features);
  RUN_TEST(test_featured_code_single_feature);
  RUN_TEST(test_featured_code_combined_features);
  RUN_TEST(test_featured_code_later_feature_wins);
  RUN_TEST(test_featured_code_configuration_order);
  RUN_TEST(test_featured_code_unknown_value);
  RUN_TEST(test_minimal_remote);
  RUN_TEST(test_feature_longer_than_template);
  unloadConfig();
  return TEST_RESULT();
}
