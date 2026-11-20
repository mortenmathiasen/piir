#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "parson.h"
#include "log.h"

static JSON_Object *ROOT_OBJECT = 0;
static char *CONFIGFILEPATH = 0;
// The definition (the remote itself or one of its "variants") the last
// loadFeaturedCode() used
static JSON_Object *SELECTED = 0;

void unloadConfig() {
  if (ROOT_OBJECT) {
    json_value_free(json_object_get_wrapping_value(ROOT_OBJECT));
    ROOT_OBJECT = 0;
  }
  SELECTED = 0;
  if (CONFIGFILEPATH) {
      free(CONFIGFILEPATH);
      CONFIGFILEPATH = 0;
  }
}

void loadConfig(const char *configfilepath) {
  if (CONFIGFILEPATH &&
      configfilepath &&
      strcmp(configfilepath,CONFIGFILEPATH)==0)
    return;

  JSON_Value *root_value = json_parse_file(configfilepath);
  if (json_value_get_type(root_value) != JSONObject) {
    log_error("JSON file %s root not an object",configfilepath);
    if (root_value)
      json_value_free(root_value);
  } else {
    unloadConfig();
    ROOT_OBJECT = json_value_get_object(root_value);
    CONFIGFILEPATH = strdup(configfilepath);
  }
}

int isConfigLoaded() {
  return ROOT_OBJECT != 0;
}

JSON_Object* getRootObject() {
  return ROOT_OBJECT;
}

JSON_Object* getAllFeaturesObject() {
  JSON_Object *all_features_object = json_object_get_object(ROOT_OBJECT, "features");
  if (!all_features_object)
    log_error("JSON file %s features object within root object",CONFIGFILEPATH);

  return all_features_object;
}

size_t getAllFeaturesCount() {
  return json_object_get_count(getAllFeaturesObject());
}

const char * getFeatureName(size_t index) {
  JSON_Object *all_features_object = getAllFeaturesObject();
  const char *result = json_object_get_name(all_features_object, index);
  return result;
}

JSON_Object * getFeatureObject(size_t index) {
  JSON_Object *all_features_object = getAllFeaturesObject();
  const char *feature_name = json_object_get_name(all_features_object, index);
  JSON_Object *feature_object = json_object_get_object(all_features_object, feature_name);
  return feature_object;
}

size_t getFeatureValuesCount(size_t nameIndex) {
  JSON_Object *feature_object = getFeatureObject(nameIndex);
  return json_object_get_count(feature_object);
}

const char * getFeatureValueName(size_t nameIndex, size_t valueIndex) {
  JSON_Object *feature_object = getFeatureObject(nameIndex);
  return json_object_get_name(feature_object, valueIndex);
}

int getFeatureIndex(const char *featureName, size_t *index) {
  size_t featuresCount = getAllFeaturesCount();
  for (size_t i = 0 ; i < featuresCount ; i++)
    if (strcmp(featureName, getFeatureName(i)) == 0) {
      *index = i;
      return true;
    }
  return false;
}

int hasFeatureNameAndValue(const char *featureName, const char *featureValue) {
  // Search for featureName
  size_t featuresCount = getAllFeaturesCount();
  size_t featureNameIndex=0;
  for (featureNameIndex=0 ; featureNameIndex<featuresCount ; featureNameIndex++)
    if (strcmp(featureName, getFeatureName(featureNameIndex))==0)
      break;
  if (featureNameIndex>=featuresCount)
    return false; //No featureName exists

  // Search for featureValue
  size_t valuesCount = getFeatureValuesCount(featureNameIndex);
  for (size_t valueIndex=0 ; valueIndex<valuesCount ; valueIndex++)
    if (strcmp(featureValue, getFeatureValueName(featureNameIndex, valueIndex))==0)
      return true;

  return false; //No featureName exists
}

JSON_Object * getSymboldefinitionsObject() {
  JSON_Object *ROOT_OBJECT = getRootObject();
  return json_object_get_object(ROOT_OBJECT, "symboldefinitions");
}

size_t getSymboldefinitionsCount() {
  JSON_Object *symboldefs_object = getSymboldefinitionsObject();
  return json_object_get_count(symboldefs_object);
}

void loadConfigText(const char *inputKey,
		    const char **outputValue)
{
  //Load roots simple members
  *outputValue = json_object_dotget_string(ROOT_OBJECT, inputKey);
}

void loadConfigNumber(const char *inputKey,
		      double *outputValue)
{
  //Load roots simple members
  *outputValue = json_object_dotget_number(ROOT_OBJECT, inputKey);
}

void loadOutPin(unsigned int *outPin)   //Output
{
  double outputValue = 0;
  loadConfigNumber("transmitter.outpin", &outputValue);
  *outPin = (unsigned int)outputValue;
}

void loadDescription(const char **description)
{
  loadConfigText("description", description);
}

void loadDevice(const char **device)
{
  loadConfigText("transmitter.device", device);
}

void loadPlugin(const char **plugin)
{
  loadConfigText("plugin", plugin);
}


void loadFrequency(int *frequency)
{
  double outputValue = 0;
  loadConfigNumber("frequency", &outputValue);
  *frequency = (int)outputValue;
}

void loadDutyCycle(double *dutycycle)
{
  loadConfigNumber("dutycycle", dutycycle);
}

JSON_Array *loadChecksums(void)
{
  return json_object_get_array(SELECTED ? SELECTED : ROOT_OBJECT, "checksums");
}

int hasVariants(void)
{
  return json_array_get_count(json_object_get_array(ROOT_OBJECT, "variants")) > 0;
}

void loadTemplateCode(const char **symbolString)
{
  loadConfigText("template", symbolString);
}

void loadConfigSymbols(symbolDefinition configSymbols[]) {
  //Load root object
  JSON_Object *ROOT_OBJECT = getRootObject();

  //Load symbol definitions
  JSON_Object *symbolsdef_object = json_object_get_object(ROOT_OBJECT, "symboldefinitions");
  if (!symbolsdef_object)
    log_error("Symbol definitions configuraiton not found");
  else {
    size_t noOfSymbolDefs = json_object_get_count(symbolsdef_object);
    for (size_t i=0 ; i<noOfSymbolDefs ; i++) {
      const char * symbol = json_object_get_name(symbolsdef_object, i);
      JSON_Object *symboldef_object = json_object_get_object(symbolsdef_object, symbol);
      configSymbols[i].symbol = symbol[0];
      /* char markPath[7]; */
      /* char spacePath[8]; */
      /* sprintf(markPath,"%s.mark",symbol); */
      /* sprintf(spacePath,"%s.space",symbol); */
      /* JSON_Value * val = json_object_get_value(symbolsdef_object,markPath); */
      configSymbols[i].markDuration = json_object_get_number(symboldef_object,"mark");
      configSymbols[i].spaceDuration = json_object_get_number(symboldef_object,"space");
    }
  }
}

// Apply the requested features to the template, then the "overrides" whose
// features are all requested. When the "when" features of one of the
// "variants" are all requested, the first such variant's template, features,
// overrides (and checksums, see loadChecksums()) are used instead. features
// holds up to
// getAllFeaturesCount() entries, unused ones with a NULL value. Features are
// applied in the order of the configuration file, so a feature defined later
// (e.g. an option forcing a fan speed) overrides an earlier one regardless of
// the order they were requested in. If a feature is requested more than once,
// the last request is used.
void loadFeaturedCode(const featureT *features,
		      char **featuredCode)
{
  loadToggledFeaturedCode(features, 0, featuredCode);
}

int hasToggle() {
  return json_object_get_count(json_object_get_object(ROOT_OBJECT, "toggle")) > 0;
}

// Copy the symbols of an overlay to code, except spaces. A shorter overlay
// leaves the rest of the code as it is.
static void applyOverlay(const char *overlay, char *code, size_t codeLength)
{
  size_t length = strlen(overlay);
  if (length > codeLength)
    log_warn("Feature code '%s' is longer than the template (%zu symbols)", overlay, codeLength);
  for (size_t i = 0; i < length && i < codeLength; i++)
    if (overlay[i] != ' ')
      code[i] = overlay[i];
}

// The requested value of a feature, NULL when it is not requested
static const char *requestedValue(const featureT *features, size_t count, const char *name)
{
  const char *value = 0;
  for (size_t i = 0; i < count; i++)
    if (features[i].name && features[i].value && strcmp(features[i].name, name) == 0)
      value = features[i].value;
  return value;
}

// All features of a "when" object are requested with its values
static int whenMatches(JSON_Object *when, const featureT *features, size_t count)
{
  for (size_t j = 0; j < json_object_get_count(when); j++) {
    const char *name = json_object_get_name(when, j);
    const char *wanted = json_object_get_string(when, name);
    const char *value = requestedValue(features, count, name);
    if (!wanted || !value || strcmp(wanted, value) != 0)
      return 0;
  }
  return 1;
}

// Apply the "overrides" of a definition whose "when" features are all
// requested with the given values, in order, after the features
static void applyOverrides(JSON_Object *definition, const featureT *features, size_t count,
			   char *code, size_t codeLength)
{
  JSON_Array *overrides = json_object_get_array(definition, "overrides");
  for (size_t i = 0; i < json_array_get_count(overrides); i++) {
    JSON_Object *override = json_array_get_object(overrides, i);
    JSON_Object *when = json_object_get_object(override, "when");
    const char *overlay = json_object_get_string(override, "code");
    if (!when || !overlay) {
      log_error("Override %zu in %s needs \"when\" and \"code\"", i, CONFIGFILEPATH);
      continue;
    }
    if (whenMatches(when, features, count))
      applyOverlay(overlay, code, codeLength);
  }
}

// The first of the "variants" whose "when" features are all requested, or
// the remote itself
static JSON_Object *selectDefinition(const featureT *features, size_t count)
{
  JSON_Array *variants = json_object_get_array(ROOT_OBJECT, "variants");
  for (size_t i = 0; features && i < json_array_get_count(variants); i++) {
    JSON_Object *variant = json_array_get_object(variants, i);
    JSON_Object *when = json_object_get_object(variant, "when");
    if (!when || !json_object_get_string(variant, "template")) {
      log_error("Variant %zu in %s needs \"when\" and \"template\"", i, CONFIGFILEPATH);
      continue;
    }
    if (whenMatches(when, features, count))
      return variant;
  }
  return ROOT_OBJECT;
}

// Like loadFeaturedCode(). When toggled is set, a feature value that has a
// code in the "toggle" section uses that code instead: remotes with a
// toggle bit send it on every other press.
void loadToggledFeaturedCode(const featureT *features, int toggled,
			     char **featuredCode)
{
  // Requested features are stored at the index of the remote's features
  size_t requestedCount = getAllFeaturesCount();
  JSON_Object *definition = selectDefinition(features, requestedCount);
  SELECTED = definition;

  const char *templateCode = json_object_get_string(definition, "template");
  if (!templateCode) {
    log_error("JSON file %s has no template", CONFIGFILEPATH);
    templateCode = "";
  }
  size_t templateStrLength = strlen(templateCode);
  *featuredCode = strdup(templateCode);
  if (!features)
    return;

  // A variant has its own features, if any: their codes refer to its template
  JSON_Object *features_object = definition == ROOT_OBJECT ? getAllFeaturesObject()
    : json_object_get_object(definition, "features");
  size_t featuresCount = json_object_get_count(features_object);
  for (size_t configIndex = 0 ; configIndex < featuresCount ; configIndex++) {
    const char *featureName = json_object_get_name(features_object, configIndex);
    const char *featureValue = requestedValue(features, requestedCount, featureName);
    if (!featureValue)
      continue;

    JSON_Object *feature_object = json_object_get_object(features_object, featureName);
    const char *featureString = json_object_get_string(feature_object, featureValue);
    if (featureString && toggled && definition == ROOT_OBJECT) {
      JSON_Object *toggle_object = json_object_get_object(
	json_object_get_object(ROOT_OBJECT, "toggle"), featureName);
      const char *toggleString = json_object_get_string(toggle_object, featureValue);
      if (toggleString)
	featureString = toggleString;
    }
    if (!featureString) {
      log_error("Feature '%s'='%s' not found in %s", featureName, featureValue, CONFIGFILEPATH);
      continue;
    }
    applyOverlay(featureString, *featuredCode, templateStrLength);
  }
  applyOverrides(definition, features, requestedCount, *featuredCode, templateStrLength);
}
