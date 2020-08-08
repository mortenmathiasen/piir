#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "parson.h"
#include "log.h"

static JSON_Object *ROOT_OBJECT = 0;
static char *CONFIGFILEPATH = 0;

void unloadConfig() {
  if (ROOT_OBJECT) {
    json_value_free((JSON_Value*)ROOT_OBJECT);
    ROOT_OBJECT = 0;
  }
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

void loadFeaturedCode(const featureT *features,
		      char **featuredCode)
{  
  //Load root object
  JSON_Object *ROOT_OBJECT = getRootObject();

  //Apply requested features to symbol string
  const char *templateCode = 0;
  loadTemplateCode(&templateCode);
  size_t templateStrLength = strlen(templateCode);
  *featuredCode = strdup(templateCode);
  JSON_Object *features_object = json_object_get_object(ROOT_OBJECT, "features");
  size_t featuresArrayLength = json_object_get_count(features_object);
  for (size_t featureNumber=0 ; featureNumber<featuresArrayLength && features ; featureNumber++) {
    const char *featureValue = features[featureNumber].value;
    if (!featureValue)
      continue;
    const char *featureName = features[featureNumber].name;
    char *featureValuePath = calloc(strlen(featureName)+strlen(featureValue)+2, 2);
    strcpy(featureValuePath, featureName);
    strcat(featureValuePath, ".");
    strcat(featureValuePath, featureValue);
    const char *featureString = json_object_dotget_string(features_object, featureValuePath);
    free(featureValuePath);
    size_t featureStrLength = strlen(featureString);
    if (featureStrLength!=templateStrLength) {
      log_warn("Length of feature code '%s' does match 'template' length %zu",
  		featureString, templateStrLength);
    }
    for (int charNumber=0 ; charNumber<featureStrLength && charNumber<templateStrLength ; charNumber++) {
      if (featureString[charNumber]!=' ')
      	(*featuredCode)[charNumber] = featureString[charNumber];
    }
  }
}
